#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <vector>

#define GPIO 2    // Pin for the electromagnet
#define BUTTON 4  // Pin for the button

// LED pins the user wants to control
const int LED_PINS[] = {13, 12, 14};
const size_t LED_PIN_COUNT = sizeof(LED_PINS) / sizeof(LED_PINS[0]);

bool isAllowedLedPin(int pin) {
    for (size_t i = 0; i < LED_PIN_COUNT; ++i)
        if (LED_PINS[i] == pin) return true;
    return false;
}

void handleLed();

// Replace with your own network credentials (kept from original)
const char* ssid = "OnePlus 8";
const char* password = "Streym2002";

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

const int GRID_SIZE = 16;
String gridState[GRID_SIZE][GRID_SIZE];

// Forward declarations
void printGridToSerial();
String normalizeColorString(const String& in);

void sendCorsHeaders() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleOptions() {
    sendCorsHeaders();
    server.send(204);
}

// Scan the gridState and turn on LEDs for colors that are present.
// Mapping: pin 13 -> red, pin 12 -> blue, pin 14 -> yellow
void updateLedsFromGrid() {
    // Only use the incoming color names (e.g. "Red", "Blue", "Yellow")
    // to decide which LEDs to light. This assumes the client sends names for
    // both compact palettes and websocket cell updates.
    bool redUsed = false;
    bool blueUsed = false;
    bool yellowUsed = false;

    for (int r = 0; r < GRID_SIZE; ++r) {
        for (int c = 0; c < GRID_SIZE; ++c) {
            const String& s = gridState[r][c];
            if (s.length() == 0) continue;
            String low = s;
            low.toLowerCase();
            if (low.indexOf("red") >= 0) redUsed = true;
            if (low.indexOf("blue") >= 0) blueUsed = true;
            if (low.indexOf("yellow") >= 0) yellowUsed = true;
            if (redUsed && blueUsed && yellowUsed) break;
        }
        if (redUsed && blueUsed && yellowUsed) break;
    }

    digitalWrite(LED_PINS[0], redUsed ? HIGH : LOW);
    digitalWrite(LED_PINS[1], blueUsed ? HIGH : LOW);
    digitalWrite(LED_PINS[2], yellowUsed ? HIGH : LOW);

    Serial.printf("updateLedsFromGrid -> red:%s blue:%s yellow:%s\n",
                  redUsed ? "ON" : "OFF", blueUsed ? "ON" : "OFF",
                  yellowUsed ? "ON" : "OFF");
}

void handleGridPost() {
    sendCorsHeaders();

    String body = server.arg("plain");
    if (body.length() == 0) {
        server.send(400, "text/plain", "Empty body");
        return;
    }

    // Protect against extremely large payloads that can exhaust RAM/stack
    if (body.length() > 32768) {
        server.send(413, "text/plain", "Payload too large");
        Serial.printf("Rejected POST /grid: payload %u bytes > limit\n",
                      (unsigned)body.length());
        return;
    }

    // Use a DynamicJsonDocument sized from the incoming payload to avoid
    // allocating large static buffers on the stack (which can cause crashes).
    size_t capacity = std::max((size_t)4096, (size_t)body.length() * 2);
    if (capacity > 65536) capacity = 65536;
    DynamicJsonDocument doc(capacity);
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        // If there wasn't enough memory to parse, return a distinct 413 so the
        // client can take action (shorten payload / send hex colors / reduce
        // size).
        if (err == DeserializationError::NoMemory) {
            Serial.println("JSON parse error in /grid: NoMemory");
            server.send(413, "text/plain",
                        String("JSON parse error: NoMemory - increase "
                               "allocation or reduce payload size"));
            return;
        }
        server.send(400, "text/plain",
                    String("JSON parse error: ") + err.c_str());
        Serial.print("JSON parse error in /grid: ");
        Serial.println(err.c_str());
        return;
    }

    // --- Support compact payloads early: { "compact": "..256chars..",
    // "palette": ["#ffffff",...] }
    if (doc.containsKey("compact") && doc["compact"].is<const char*>()) {
        const char* comp = doc["compact"];
        if (!comp) {
            server.send(400, "text/plain", "compact must be a string");
            return;
        }
        String compStr = String(comp);
        if (compStr.length() != GRID_SIZE * GRID_SIZE) {
            Serial.printf("POST /grid compact wrong length: %u\n",
                          (unsigned)compStr.length());
            server.send(400, "text/plain", "compact string must be 256 chars");
            return;
        }
        // palette optional
        std::vector<String> pal;
        if (doc.containsKey("palette") && doc["palette"].is<JsonArray>()) {
            JsonArray parr = doc["palette"].as<JsonArray>();
            for (size_t i = 0; i < parr.size(); ++i) {
                if (parr[i].is<const char*>()) {
                    String raw = String((const char*)parr[i]);
                    // Normalize palette entries: prefer mapping names to hex
                    String norm = normalizeColorString(raw);
                    pal.push_back(norm);
                } else {
                    pal.push_back(String(""));
                }
            }
        }

        auto decodeIndex = [](char ch) -> int {
            const char* chars =
                "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxy"
                "z";
            const char* p = strchr(chars, ch);
            if (!p) return -1;
            return (int)(p - chars);
        };

        for (int i = 0; i < GRID_SIZE * GRID_SIZE; ++i) {
            char ch = compStr.charAt(i);
            int r = i / GRID_SIZE;
            int c = i % GRID_SIZE;
            if (ch == '.') {
                gridState[r][c] = "";
            } else {
                int idx = decodeIndex(ch);
                if (idx < 0 || (size_t)idx >= pal.size()) {
                    Serial.printf(
                        "POST /grid compact unknown palette idx %d at pos %d\n",
                        idx, i);
                    server.send(400, "text/plain",
                                "compact contains invalid palette index");
                    return;
                }
                const String col = pal[idx];
                if (col.length() > 64) {
                    Serial.printf(
                        "POST /grid compact palette string too long idx %d len "
                        "%u\n",
                        idx, (unsigned)col.length());
                    server.send(400, "text/plain", "palette string too long");
                    return;
                }
                gridState[r][c] = col;
            }
        }

        printGridToSerial();
        updateLedsFromGrid();
        server.send(200, "application/json", "{\"status\":\"ok\"}");
        return;
    }

    if (!doc.containsKey("grid")) {
        Serial.println("POST /grid missing 'grid' property");
        server.send(400, "text/plain", "Missing 'grid' property");
        return;
    }

    if (!doc.containsKey("grid") || !doc["grid"].is<JsonArray>()) {
        Serial.println("POST /grid 'grid' not an array");
        server.send(400, "text/plain", "'grid' must be an array");
        return;
    }

    JsonArray grid = doc["grid"].as<JsonArray>();
    if ((int)grid.size() != GRID_SIZE) {
        Serial.printf("POST /grid wrong row count: %u\n",
                      (unsigned)grid.size());
        server.send(400, "text/plain", "'grid' must have 16 rows");
        return;
    }

    for (int r = 0; r < GRID_SIZE; ++r) {
        if (!grid[r].is<JsonArray>()) {
            Serial.printf("POST /grid row %d not an array\n", r);
            server.send(400, "text/plain", "Each grid row must be an array");
            return;
        }
        JsonArray row = grid[r].as<JsonArray>();
        if ((int)row.size() != GRID_SIZE) {
            Serial.printf("POST /grid row %d wrong column count: %u\n", r,
                          (unsigned)row.size());
            server.send(400, "text/plain",
                        "Each grid row must have 16 columns");
            return;
        }
        for (int c = 0; c < GRID_SIZE; ++c) {
            if (row[c].isNull()) {
                gridState[r][c] = "";
            } else if (row[c].is<const char*>()) {
                const char* s = row[c];
                // Optional: limit string length to avoid enormous allocations
                if (strlen(s) > 64) {
                    Serial.printf(
                        "POST /grid row %d col %d color string too long (%u)\n",
                        r, c, (unsigned)strlen(s));
                    server.send(400, "text/plain", "Color string too long");
                    return;
                }
                String col = String(s);
                // Normalize color names like "Red" -> "#e53935", and normalize
                // hex
                col = normalizeColorString(col);
                gridState[r][c] = col;
            } else {
                // Not a string or null
                Serial.printf("POST /grid row %d col %d invalid value type\n",
                              r, c);
                server.send(400, "text/plain",
                            "Grid values must be strings or null");
                return;
            }
        }
    }

    Serial.println("Received grid:");
    for (int r = 0; r < GRID_SIZE; ++r) {
        String line = "";
        for (int c = 0; c < GRID_SIZE; ++c) {
            line += gridState[r][c].length() ? "X" : ".";
        }
        Serial.println(line);
    }
    updateLedsFromGrid();
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

// Simple status endpoint to help the web client perform reachability checks
void handleStatus() {
    sendCorsHeaders();
    if (WiFi.status() == WL_CONNECTED) {
        String ip = WiFi.localIP().toString();
        String payload =
            String("{\"status\":\"ok\",\"ip\":\"") + ip + String("\"}");
        server.send(200, "application/json", payload);
    } else {
        server.send(200, "application/json",
                    "{\"status\":\"offline\",\"ip\":\"\"}");
    }
}

// Return the color associated with an LED pin
const char* getLedColorName(int pin) {
    if (pin == 13) return "red";
    if (pin == 12) return "blue";
    if (pin == 14) return "yellow";
    return "unknown";
}

// Normalize color strings: map known color names to canonical hex used by the
// client, normalize hex to lowercase and expand short form (#RGB) to #RRGGBB.
String normalizeColorString(const String& in) {
    String s = in;
    s.trim();
    if (s.length() == 0) return s;
    // If hex, normalize to lowercase and expand short form
    if (s.charAt(0) == '#') {
        s.toLowerCase();
        if (s.length() == 4) {
            // #RGB -> #RRGGBB
            char r = s.charAt(1);
            char g = s.charAt(2);
            char b = s.charAt(3);
            String out = "#";
            out += String(r);
            out += String(r);
            out += String(g);
            out += String(g);
            out += String(b);
            out += String(b);
            out.toLowerCase();
            return out;
        }
        return s;
    }
    // For non-hex input (e.g. "Red", "Blue"), preserve the incoming value
    // as-is (trimmed). This lets the incoming palette use names instead of
    // being converted to hex.
    return s;
}

// HTTP handler for /led
void handleLed() {
    sendCorsHeaders();

    // If no pin specified, return the list of managed pins and their states
    if (!server.hasArg("pin") && server.method() == HTTP_GET) {
        DynamicJsonDocument resp(1024);
        JsonArray arr = resp.to<JsonArray>();
        for (size_t i = 0; i < LED_PIN_COUNT; ++i) {
            int pin = LED_PINS[i];
            JsonObject obj = arr.createNestedObject();
            obj["pin"] = pin;
            obj["color"] = getLedColorName(pin);
            obj["state"] = (digitalRead(pin) == HIGH) ? "on" : "off";
        }
        String out;
        serializeJson(arr, out);
        server.send(200, "application/json", out);
        return;
    }

    // Determine pin from query param or JSON body
    int pin = -1;
    String stateStr = "";

    if (server.hasArg("pin")) {
        pin = server.arg("pin").toInt();
        if (server.hasArg("state")) stateStr = server.arg("state");
    } else {
        // Try JSON body
        String body = server.arg("plain");
        if (body.length() > 0) {
            size_t cap = std::max((size_t)256, (size_t)body.length() * 2);
            if (cap > 2048) cap = 2048;
            DynamicJsonDocument doc(cap);
            DeserializationError err = deserializeJson(doc, body);
            if (!err) {
                if (doc.containsKey("pin")) pin = doc["pin"] | -1;
                if (doc.containsKey("state") && doc["state"].is<const char*>())
                    stateStr = String((const char*)doc["state"]);
            }
        }
    }

    if (pin < 0 || !isAllowedLedPin(pin)) {
        server.send(400, "text/plain",
                    "Invalid or missing 'pin' (allowed: 13,12,14)");
        return;
    }

    // If no state provided, toggle
    if (stateStr.length() == 0) {
        int cur = digitalRead(pin);
        int nw = (cur == HIGH) ? LOW : HIGH;
        digitalWrite(pin, nw);
    } else {
        String s = stateStr;
        s.toLowerCase();
        if (s == "on" || s == "1" || s == "true") {
            digitalWrite(pin, HIGH);
        } else if (s == "off" || s == "0" || s == "false") {
            digitalWrite(pin, LOW);
        } else if (s == "toggle") {
            int cur = digitalRead(pin);
            digitalWrite(pin, (cur == HIGH) ? LOW : HIGH);
        } else {
            server.send(400, "text/plain",
                        "Invalid state value (use on/off/toggle)");
            return;
        }
    }

    // Respond with the current state
    DynamicJsonDocument resp(256);
    resp["pin"] = pin;
    resp["color"] = getLedColorName(pin);
    resp["state"] = (digitalRead(pin) == HIGH) ? "on" : "off";
    String out;
    serializeJson(resp, out);
    server.send(200, "application/json", out);
}

// Helper to print grid (used from websocket handlers too)
void printGridToSerial() {
    // Build palette of unique non-empty colors in gridState so we can print
    // indices
    std::vector<String> palette;
    for (int r = 0; r < GRID_SIZE; ++r) {
        for (int c = 0; c < GRID_SIZE; ++c) {
            const String& s = gridState[r][c];
            if (s.length() == 0) continue;
            bool found = false;
            for (size_t i = 0; i < palette.size(); ++i) {
                if (palette[i] == s) {
                    found = true;
                    break;
                }
            }
            if (!found) palette.push_back(s);
        }
    }

    Serial.println(
        "Grid state (numbers refer to palette indices, '.' is empty):");
    for (int r = 0; r < GRID_SIZE; ++r) {
        String line = "";
        for (int c = 0; c < GRID_SIZE; ++c) {
            const String& s = gridState[r][c];
            if (s.length() == 0) {
                line += ". ";
            } else {
                int idx = -1;
                for (size_t i = 0; i < palette.size(); ++i)
                    if (palette[i] == s) {
                        idx = (int)i;
                        break;
                    }
                if (idx < 0) {
                    line += "? ";
                } else {
                    if (idx < 10)
                        line += String(idx) + " ";
                    else
                        line += String(idx) + " ";
                }
            }
        }
        Serial.println(line);
    }

    if (!palette.empty()) {
        Serial.print("Palette: ");
        for (size_t i = 0; i < palette.size(); ++i) {
            // print like 0=#ffffff, 1=#000000
            Serial.printf("%u=%s", (unsigned)i, palette[i].c_str());
            if (i + 1 < palette.size()) Serial.print(", ");
        }
        Serial.println();
    }
}

// Handle incoming websocket messages (text)
void onWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t* payload,
                      size_t length) {
    if (type == WStype_TEXT) {
        // Basic safety: reject absurdly large messages to avoid exhausting heap
        Serial.printf("WS incoming text length=%u\n", (unsigned)length);
        if (length == 0 || length > 16384) {
            Serial.println("WS message too large or empty, ignoring");
            return;
        }

        String msg = String((char*)payload);
        // Expect messages like: {"type":"cell","r":1,"c":2,"color":"#ffffff"}
        // or {"type":"full","grid":[...]}
        // Use a dynamic document sized from the incoming payload to avoid
        // stack/heap corruption.
        size_t capacity = std::max((size_t)2048, length * 2);
        if (capacity < 4096) capacity = 4096;
        if (capacity > 32768) capacity = 32768;
        DynamicJsonDocument doc(capacity);
        DeserializationError err = deserializeJson(doc, msg);
        if (err) {
            Serial.print("WS JSON parse error: ");
            Serial.println(err.c_str());
            return;
        }

        const char* typeStr = doc["type"] | "";
        if (strcmp(typeStr, "cell") == 0) {
            int r = doc["r"] | -1;
            int c = doc["c"] | -1;
            if (r >= 0 && c >= 0 && r < GRID_SIZE && c < GRID_SIZE) {
                if (doc["color"].isNull()) {
                    gridState[r][c] = "";
                } else {
                    String col = String((const char*)doc["color"]);
                    col = normalizeColorString(col);
                    gridState[r][c] = col;
                }
                // For feedback, print the updated row
                Serial.printf("WS cell update: r=%d c=%d color=%s\n", r, c,
                              gridState[r][c].c_str());
                // Update LEDs because a cell changed
                updateLedsFromGrid();
            }
        } else if (strcmp(typeStr, "full") == 0) {
            if (!doc.containsKey("grid")) return;
            JsonArray grid = doc["grid"].as<JsonArray>();
            for (int r = 0; r < GRID_SIZE; ++r) {
                JsonArray row = grid[r].as<JsonArray>();
                for (int c = 0; c < GRID_SIZE; ++c) {
                    if (row[c].isNull())
                        gridState[r][c] = "";
                    else
                        gridState[r][c] = String((const char*)row[c]);
                }
            }
            printGridToSerial();
            updateLedsFromGrid();
        }
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(GPIO, OUTPUT);
    pinMode(BUTTON, INPUT);

    // Initialize LED pins
    for (size_t i = 0; i < LED_PIN_COUNT; ++i) {
        pinMode(LED_PINS[i], OUTPUT);
        digitalWrite(LED_PINS[i], LOW);
    }

    WiFi.mode(WIFI_STA);
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            Serial.print("Local ESP32 IP: ");
            Serial.println(WiFi.localIP());
        }
    });

    WiFi.begin(ssid, password);
    Serial.println("\nConnecting to WiFi Network ..");

    // Wait for WiFi with timeout so server starts after IP is assigned
    const unsigned long wifiTimeout = 15000;  // ms
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < wifiTimeout) {
        delay(200);
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("Connected — IP: ");
        Serial.println(WiFi.localIP());

        // Start HTTP server routes after WiFi is up
        server.on("/grid", HTTP_OPTIONS, handleOptions);
        server.on("/grid", HTTP_POST, handleGridPost);
        server.on("/led", HTTP_GET, handleLed);
        server.on("/led", HTTP_POST, handleLed);
        server.on("/status", HTTP_GET, handleStatus);
        server.begin();
        Serial.println("HTTP server started");

        // Start WebSocket server
        webSocket.begin();
        webSocket.onEvent(onWebSocketEvent);
        Serial.println("WebSocket server started on port 81");
    } else {
        Serial.println(
            "WiFi connect timed out — servers not started. Check "
            "credentials/network.");
        // Still set up routes so HTTP might work if IP becomes available later
        server.on("/grid", HTTP_OPTIONS, handleOptions);
        server.on("/grid", HTTP_POST, handleGridPost);
        server.on("/status", HTTP_GET, handleStatus);
    }
}

void loop() {
    server.handleClient();
    webSocket.loop();

    // Periodic status output to help debugging connectivity
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 5000) {
        lastPrint = millis();
        Serial.print("WiFi status: ");
        Serial.println(WiFi.status());
    }
}