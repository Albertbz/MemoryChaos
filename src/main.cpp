#include <Arduino.h>

#define GPIO 2          // Pin for the electromagnet
#define BUTTON 4       // Pin for the button

#include <WiFi.h>
 
// Replace with your own network credentials
const char* ssid = "OnePlus 8";
const char* password = "Streym2002";


void ConnectedToAP_Handler(WiFiEvent_t wifi_event, WiFiEventInfo_t wifi_info) {
  Serial.println("Connected To The WiFi Network");
}
 
void GotIP_Handler(WiFiEvent_t wifi_event, WiFiEventInfo_t wifi_info) {
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  pinMode(GPIO, OUTPUT);    // Set LED pin as output
  pinMode(BUTTON, INPUT);  // Set button pin as input

 
    Serial.begin(115200);
 
    WiFi.mode(WIFI_STA);
    WiFi.mode(WIFI_STA);
  WiFi.onEvent(ConnectedToAP_Handler, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(GotIP_Handler, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.begin(ssid, password);
  Serial.println("\nConnecting to WiFi Network ..");

}

void loop() {
  /*int buttonState = digitalRead(BUTTON); // Read the button state

  if (buttonState == HIGH) { // If button is pressed
    digitalWrite(GPIO, HIGH); // Turn on the electromagnet
    Serial.println("Electromagnet is ON");
  } else { // If button is not pressed
    digitalWrite(GPIO, LOW);  // Turn off the electromagnet
    Serial.println("Electromagnet is OFF");
  }

  delay(100); // Small delay to debounce the button*/
}