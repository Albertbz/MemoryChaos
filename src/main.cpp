#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <vector>

// NOTE: avoid using GPIOs 34-39 (input-only) and 6-11 (flash) on ESP32.
// Use only output-capable pins here.
const int LED_ROWS[] = {13, 12, 14, 27, 26};
// Replaced input-only pins (34,35) with safe output-capable pins
const int LED_COLS[] = {25, 33, 32, 23, 19};
const int LED_MATRIX_COUNT = 5;
const int SPEAKER_PIN = 15;
const int RELAY_PINS[] = {2, 4};

int pixel = 0;
bool countDownInProgress = false;
int timeBetweenBeeps = 0;
static unsigned long countdownStart = 0;
static unsigned long timeUntilNextStage = 10000;
const int pixelDelay = 1;  // milliseconds

std::vector<std::vector<int>> activePixels;
std::vector<std::vector<int>> prevActivePixels;

// Replace with your own network credentials (kept from original)
const char* ssid = "Alberts iPhone 15 Pro";
const char* password = "tingting";

WebServer server(80);

const int GRID_SIZE = 16;
String gridState[GRID_SIZE][GRID_SIZE];

// Forward
void setCountdownBeeps(bool activate, int _timeBetweenBeeps);

void sendCorsHeaders() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleOptions() {
    sendCorsHeaders();
    server.send(204);
}

void handleSimpleGrid() {
    sendCorsHeaders();
    String body = server.arg("plain");
    if (body.length() == 0) {
        server.send(400, "text/plain", "Empty body");
        return;
    }

    // Protect against extremely large payloads
    if (body.length() > 32768) {
        server.send(413, "text/plain", "Payload too large");
        Serial.printf("Rejected POST /grid-simple: payload %u bytes > limit\n",
                      (unsigned)body.length());
        return;
    }

    // Parse JSON body expecting { "compact": "0101..." }
    size_t capacity = std::max((size_t)256, (size_t)body.length() * 2);
    if (capacity > 65536) capacity = 65536;
    DynamicJsonDocument doc(capacity);
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        if (err == DeserializationError::NoMemory) {
            Serial.println("JSON parse error in /grid-simple: NoMemory");
            server.send(
                413, "text/plain",
                String("JSON parse error: NoMemory - reduce payload size"));
            return;
        }
        Serial.print("JSON parse error in /grid-simple: ");
        Serial.println(err.c_str());
        server.send(400, "text/plain",
                    String("JSON parse error: ") + err.c_str());
        return;
    }

    if (!doc.containsKey("compact") || !doc["compact"].is<const char*>()) {
        server.send(400, "text/plain", "Missing or invalid 'compact' property");
        return;
    }

    const char* comp = doc["compact"];
    if (!comp) {
        server.send(400, "text/plain", "compact must be a string");
        return;
    }
    String compStr = String(comp);

    // Clear entire grid first to avoid stale cells
    for (int r = 0; r < GRID_SIZE; ++r) {
        for (int c = 0; c < GRID_SIZE; ++c) gridState[r][c] = "";
    }

    // If client sent a 5x5 compact string (LED_MATRIX_COUNT^2), map it to
    // the top-left region of the 16x16 grid. If a full 16x16 (256) string
    // is sent, populate the full grid.
    size_t smallSz = (size_t)LED_MATRIX_COUNT * (size_t)LED_MATRIX_COUNT;
    size_t bigSz = (size_t)GRID_SIZE * (size_t)GRID_SIZE;

    if (compStr.length() == smallSz) {
        for (size_t i = 0; i < smallSz; ++i) {
            char ch = compStr.charAt(i);
            if (ch != '0' && ch != '1') {
                server.send(400, "text/plain",
                            "compact must contain only '0' and '1'");
                return;
            }
            int r = i / LED_MATRIX_COUNT;
            int c = i % LED_MATRIX_COUNT;
            if (ch == '1') {
                // Presence-only payload: use a simple marker color (white)
                gridState[r][c] = "#ffffff";
            } else {
                gridState[r][c] = "";
            }
        }
        // Update the activePixels vector used by the main loop so the
        // hardware will be driven according to the 5x5 compact payload.
        activePixels.clear();
        for (size_t i = 0; i < smallSz; ++i) {
            char ch = compStr.charAt(i);
            if (ch == '1') {
                int r = (int)(i / LED_MATRIX_COUNT);
                int c = (int)(i % LED_MATRIX_COUNT);
                // push row/col pair
                activePixels.push_back({r, c});
            }
        }

        server.send(200, "application/json", "{\"status\":\"ok\"}");

        // Wait for 5 seconds, then start the countdown beeps
        countdownStart = millis();

        return;
    } else if (compStr.length() == bigSz) {
        // Treat any non-'0' as filled
        for (size_t i = 0; i < bigSz; ++i) {
            char ch = compStr.charAt(i);
            int r = i / GRID_SIZE;
            int c = i % GRID_SIZE;
            if (ch == '0') {
                gridState[r][c] = "";
            } else {
                gridState[r][c] = "#ffffff";
            }
        }
        server.send(200, "application/json", "{\"status\":\"ok\"}");
        return;
    } else {
        Serial.printf("POST /grid-simple wrong length: %u\n",
                      (unsigned)compStr.length());
        server.send(400, "text/plain",
                    "compact string must be 25 (5x5) or 256 (16x16) chars");
        return;
    }
}

// Make a POST /push-magnets handler that activates the relays
void handlePushMagnets() {
    sendCorsHeaders();

    digitalWrite(RELAY_PINS[0], HIGH);
    digitalWrite(RELAY_PINS[1], HIGH);
    delay(300);
    digitalWrite(RELAY_PINS[0], LOW);
    digitalWrite(RELAY_PINS[1], LOW);

    server.send(200, "application/json", "{\"status\":\"magnets pushed\"}");

    Serial.println("POST /push-magnets: Magnets pushed");
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

void setup() {
    Serial.begin(115200);

    // Initialize LED rows and columns
    for (size_t i = 0; i < LED_MATRIX_COUNT; i++) {
        pinMode(LED_ROWS[i], OUTPUT);
        pinMode(LED_COLS[i], OUTPUT);
        digitalWrite(LED_ROWS[i], LOW);
        digitalWrite(LED_COLS[i], HIGH);
    }

    // Initialize speaker pin
    pinMode(SPEAKER_PIN, OUTPUT);

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
        server.on("/grid-simple", HTTP_OPTIONS, handleOptions);
        server.on("/grid-simple", HTTP_POST, handleSimpleGrid);
        server.on("/push-magnets", HTTP_POST, handlePushMagnets);
        server.on("/status", HTTP_GET, handleStatus);
        server.begin();
        Serial.println("HTTP server started");

    } else {
        Serial.println(
            "WiFi connect timed out — servers not started. Check "
            "credentials/network.");
        // Still set up routes so HTTP might work if IP becomes available later
        server.on("/grid-simple", HTTP_OPTIONS, handleOptions);
        server.on("/grid-simple", HTTP_POST, handleSimpleGrid);
        server.on("/status", HTTP_GET, handleStatus);
    }
}

void turnOnRowCol(int row, int col) {
    digitalWrite(LED_ROWS[row], HIGH);
    digitalWrite(LED_COLS[col], LOW);
}

void turnOffRowCol(int row, int col) {
    digitalWrite(LED_ROWS[row], LOW);
    digitalWrite(LED_COLS[col], HIGH);
}

void setCountdownBeeps(bool activate, int _timeBetweenBeeps) {
    if (activate && !countDownInProgress) {
        countDownInProgress = true;
        timeBetweenBeeps = _timeBetweenBeeps;
    } else if (!activate && countDownInProgress) {
        countDownInProgress = false;
        timeBetweenBeeps = _timeBetweenBeeps;
    }
}

void loop() {
    // If countdown is not 0, meaning it has been set to start, then check
    // whether 5 seconds have passed
    if (countdownStart != 0 && millis() - countdownStart > 5000) {
        countdownStart = 0;
        // Copy current activePixels to prevActivePixels
        prevActivePixels = activePixels;
        // and clear activePixels
        activePixels.clear();
        setCountdownBeeps(true, 8000);
    }

    // Activate and deactivate pixels in ActivePixels vector so
    // that the LED matrix shows the desired pattern.
    for (auto& p : activePixels) {
        int row = p[0];
        int col = p[1];
        turnOnRowCol(row, col);
        delay(pixelDelay);
        turnOffRowCol(row, col);
    }

    if (countDownInProgress) {
        static unsigned long lastBeep = 0;
        if (millis() - lastBeep > timeBetweenBeeps) {
            lastBeep = millis();
            Serial.print("Beep");
            tone(SPEAKER_PIN, 300);
            delay(100);
            noTone(SPEAKER_PIN);
        }

        static unsigned long lastChange = 0;
        if (millis() - lastChange > timeUntilNextStage) {
            lastChange = millis();
            switch (timeBetweenBeeps) {
                case 8000: {
                    timeBetweenBeeps = 4000;
                    break;
                }
                case 4000: {
                    timeBetweenBeeps = 2000;
                    break;
                }
                case 2000: {
                    timeBetweenBeeps = 1000;
                    break;
                }
                case 1000: {
                    timeBetweenBeeps = 500;
                    break;
                }
                case 500: {
                    timeBetweenBeeps = 250;
                    timeUntilNextStage = 5000;
                    break;
                }
                case 250: {
                    timeBetweenBeeps = 125;
                    break;
                }
                case 125: {
                    tone(SPEAKER_PIN, 200);
                    delay(2000);
                    noTone(SPEAKER_PIN);
                    tone(SPEAKER_PIN, 500);
                    delay(100);
                    noTone(SPEAKER_PIN);
                    setCountdownBeeps(false, 0);
                    activePixels = prevActivePixels;
                    timeUntilNextStage = 10000;
                    break;
                }
            }
        }
    }

    // tone(SPEAKER_PIN, 440);  // Play tone at 440 Hz
    // delay(100);              // for 100 milliseconds
    // noTone(SPEAKER_PIN);     // Stop the tone
    // delay(1000);

    server.handleClient();

    // Periodic status output to help debugging connectivity
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint > 5000) {
        lastPrint = millis();
        Serial.print("WiFi status: ");
        Serial.println(WiFi.status());
    }
}