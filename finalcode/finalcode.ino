#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>   // ✅ Needed to parse JSON response

// ============================================
// PIN CONFIGURATION
// ============================================
#define DATA_PIN  D5
#define CLOCK_PIN D7
#define LATCH_PIN D6

// ============================================
// WIFI CONFIGURATION
// ============================================
const char* ssid = "TP-Link_653C";
const char* password = "42578444";

// ============================================
// REMOTE SERVER (Node.js endpoint)
// ============================================
const char* serverHost = "http://10.185.135.31:5000/message";

// ============================================
// DISPLAY CONFIGURATION
// ============================================
#define NUM_DIGITS 15
#define REFRESH_INTERVAL 100  // Microseconds per digit
#define CHAR_BLANK 25

uint8_t displayBuffer[NUM_DIGITS];
unsigned long lastFetch = 0;
unsigned long fetchInterval = 10000;  // Every 10 seconds
unsigned long lastRefresh = 0;
uint8_t currentDigit = 0;
String displayMessage = "";

// ============================================
// SEGMENT MAP (Common Anode)
// ============================================
const uint8_t SEGMENT_MAP[26] = {
  0b11000000, // 0
  0b11111001, // 1
  0b10100100, // 2
  0b10110000, // 3
  0b10011001, // 4
  0b10010010, // 5
  0b10000010, // 6
  0b11111000, // 7
  0b10000000, // 8
  0b10010000, // 9
  0b10001000, // A
  0b10000011, // b
  0b11000110, // C
  0b10100001, // d
  0b10000110, // E
  0b10001110, // F
  0b10000010, // G
  0b10001001, // H
  0b11111001, // I
  0b11100001, // J
  0b11000111, // L
  0b11000000, // O
  0b10001100, // P
  0b10010010, // S
  0b11000001, // U
  0b11111111  // blank
};

// ============================================
// FUNCTION DECLARATIONS
// ============================================
void updateDisplay();
void writeToRegisters(uint8_t segments, uint16_t digits);
void displayText(String text);
void clearDisplay();
uint8_t charToPattern(char c);
String fetchMessage();

// ============================================
// SETUP
// ============================================
void setup() {
  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);

  Serial.begin(115200);
  clearDisplay();
  displayText("STARTING");

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi Connected!");
  Serial.println(WiFi.localIP());

  displayText("READY");
  delay(1500);
}

// ============================================
// LOOP
// ============================================
void loop() {
  updateDisplay();

  if (millis() - lastFetch > fetchInterval) {
    lastFetch = millis();
    String newMessage = fetchMessage();

    if (newMessage.length() > 0 && newMessage != displayMessage) {
      displayMessage = newMessage;
      displayText(displayMessage);
      Serial.println("Updated display to: " + displayMessage);
    }
  }
}

// ============================================
// FETCH MESSAGE FROM SERVER
// ============================================
String fetchMessage() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi not connected!");
    return "";
  }

  WiFiClient client;
  HTTPClient http;

  if (!http.begin(client, serverHost)) {
    Serial.println("❌ HTTP begin failed");
    return "";
  }

  int httpCode = http.GET();
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      payload.trim();
      Serial.println("Raw payload: " + payload);

      // ✅ Parse JSON { "message": "..." }
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        Serial.print("⚠️ JSON parse error: ");
        Serial.println(error.c_str());
        http.end();
        return "";
      }

      String msg = doc["message"].as<String>();
      msg.trim();
      http.end();
      return msg;
    }
  } else {
    Serial.printf("❌ HTTP GET failed, code: %d\n", httpCode);
  }

  http.end();
  return "";
}

// ============================================
// DISPLAY FUNCTIONS
// ============================================
void updateDisplay() {
  unsigned long now = micros();
  if (now - lastRefresh < REFRESH_INTERVAL) return;
  lastRefresh = now;

  uint8_t segIndex = displayBuffer[currentDigit];
  if (segIndex >= 26) segIndex = CHAR_BLANK;

  uint8_t segPattern = SEGMENT_MAP[segIndex];
  uint16_t digitMask = 0xFFFF;
  digitMask &= ~(1 << currentDigit);

  writeToRegisters(segPattern, digitMask);
  currentDigit = (currentDigit + 1) % NUM_DIGITS;
}

void writeToRegisters(uint8_t segments, uint16_t digits) {
  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, (digits >> 8) & 0xFF);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, digits & 0xFF);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, segments);
  digitalWrite(LATCH_PIN, HIGH);
}

void displayText(String text) {
  clearDisplay();
  text.toUpperCase();
  int len = min((int)text.length(), NUM_DIGITS);
  for (int i = 0; i < len; i++) {
    displayBuffer[i] = charToPattern(text[i]);
  }
  Serial.println("Display: " + text);
}

void clearDisplay() {
  for (int i = 0; i < NUM_DIGITS; i++) displayBuffer[i] = CHAR_BLANK;
}

uint8_t charToPattern(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  switch (c) {
    case 'A': return 10; case 'B': return 11; case 'C': return 12;
    case 'D': return 13; case 'E': return 14; case 'F': return 15;
    case 'G': return 16; case 'H': return 17; case 'I': return 18;
    case 'J': return 19; case 'L': return 20; case 'O': return 21;
    case 'P': return 22; case 'S': return 23; case 'U': return 24;
    case ' ': return 25;
    default: return 25;
  }
}
