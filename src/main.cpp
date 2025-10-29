#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>

#define GPIO 2    // Pin for the electromagnet
#define BUTTON 4  // Pin for the button

// Replace with your own network credentials (kept from original)
const char* ssid = "OnePlus 8";
const char* password = "Streym2002";

// Use port 8080 for HTTP to avoid carrier/hotspot port blocking on port 80
WebServer server(8080);
WebSocketsServer webSocket = WebSocketsServer(81);

const int GRID_SIZE = 16;
String gridState[GRID_SIZE][GRID_SIZE];

void sendCorsHeaders() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleOptions() {
    sendCorsHeaders();
    server.send(204);
}

void handleGridPost() {
    sendCorsHeaders();

    String body = server.arg("plain");
    if (body.length() == 0) {
        server.send(400, "text/plain", "Empty body");
        return;
    }

    StaticJsonDocument<8192> doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        server.send(400, "text/plain",
                    String("JSON parse error: ") + err.c_str());
        return;
    }

    if (!doc.containsKey("grid")) {
        server.send(400, "text/plain", "Missing 'grid' property");
        return;
    }

    JsonArray grid = doc["grid"].as<JsonArray>();

    for (int r = 0; r < GRID_SIZE; ++r) {
        JsonArray row = grid[r].as<JsonArray>();
        for (int c = 0; c < GRID_SIZE; ++c) {
            if (row[c].isNull()) {
                gridState[r][c] = "";
            } else {
                gridState[r][c] = String((const char*)row[c]);
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

// Helper to print grid (used from websocket handlers too)
void printGridToSerial() {
    Serial.println("Grid state:");
    for (int r = 0; r < GRID_SIZE; ++r) {
        String line = "";
        for (int c = 0; c < GRID_SIZE; ++c) {
            line += gridState[r][c].length() ? "X" : ".";
        }
        Serial.println(line);
    }
}

// Handle incoming websocket messages (text)
void onWebSocketEvent(uint8_t clientNum, WStype_t type, uint8_t* payload,
                      size_t length) {
    if (type == WStype_TEXT) {
        String msg = String((char*)payload);
        // Expect messages like: {"type":"cell","r":1,"c":2,"color":"#ffffff"}
        // or {"type":"full","grid":[...]}
        StaticJsonDocument<2048> doc;
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
                    gridState[r][c] = String((const char*)doc["color"]);
                }
                // For feedback, print the updated row
                Serial.printf("WS cell update: r=%d c=%d color=%s\n", r, c,
                              gridState[r][c].c_str());
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
        }
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(GPIO, OUTPUT);
    pinMode(BUTTON, INPUT);

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