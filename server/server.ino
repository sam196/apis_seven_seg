#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

// =====================================
// CONFIGURATION
// =====================================
const char* ssid = "TP-Link_653C";
const char* password = "42578444";
const char* serverHost = "http://10.185.135.31:5000/message";

unsigned long lastCheck = 0;
const unsigned long checkInterval = 10000;  // every 10 seconds
String currentMessage = "WELCOME TO SAMTRONICS";

// =====================================
// PIN CONFIGURATION (your display pins)
// =====================================
#define DATA_PIN  D5
#define CLOCK_PIN D7
#define LATCH_PIN D6

void connectWiFi();
String fetchMessage();
void displayText(String text);
void clearDisplay();

void setup() {
  Serial.begin(115200);
  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);

  clearDisplay();
  connectWiFi();

  Serial.println("ESP Ready. Fetching message...");
  String msg = fetchMessage();
  if (msg.length() > 0) currentMessage = msg;

  displayText(currentMessage);
}

void loop() {
  if (millis() - lastCheck >= checkInterval) {
    lastCheck = millis();
    String msg = fetchMessage();
    if (msg.length() > 0 && msg != currentMessage) {
      currentMessage = msg;
      displayText(currentMessage);
    }
  }
}

// =====================================
// CONNECT WIFI
// =====================================
void connectWiFi() {
  Serial.printf("Connecting to %s", ssid);
  WiFi.begin(ssid, password);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi Failed!");
  }
}

// =====================================
// FETCH MESSAGE FROM SERVER
// =====================================
String fetchMessage() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    return "";
  }

  WiFiClient client;
  HTTPClient http;
  http.begin(client, serverHost);  // ✅ New syntax
  int httpCode = http.GET();

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("Payload: " + payload);

      StaticJsonDocument<200> doc;
      DeserializationError err = deserializeJson(doc, payload);
      if (err) {
        Serial.print("JSON Error: ");
        Serial.println(err.c_str());
        http.end();
        return "";
      }

      String msg = doc["message"].as<String>();
      Serial.println("✅ Server Message: " + msg);
      http.end();
      return msg;
    }
  } else {
    Serial.printf("❌ HTTP Error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
  return "";
}

// =====================================
// DISPLAY HANDLING (you can replace with your LED display logic)
// =====================================
void clearDisplay() {
  Serial.println("Display cleared.");
}

void displayText(String text) {
  Serial.print("Displaying: ");
  Serial.println(text);
}
