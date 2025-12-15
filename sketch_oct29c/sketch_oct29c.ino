#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiManager.h>     // ✅ Wi-Fi setup portal

// ============================================
// PIN CONFIGURATION
// ============================================
#define DATA_PIN  D5
#define CLOCK_PIN D7
#define LATCH_PIN D6

// ============================================
// SERVER CONFIGURATION (Hosted on Render)
// ============================================
String serverBase = "https://sevensegment.onrender.com/message?id=";

// ============================================
// DISPLAY CONFIGURATION
// ============================================
#define NUM_DIGITS 15
#define REFRESH_INTERVAL 100
#define CHAR_BLANK 25

uint8_t displayBuffer[NUM_DIGITS];
unsigned long lastFetch = 0;
unsigned long fetchInterval = 10000;
unsigned long lastRefresh = 0;
uint8_t currentDigit = 0;
String displayMessage = "";
String deviceID = "";

// ============================================
// SEGMENT MAP (COMMON ANODE)
// ============================================
const uint8_t SEGMENT_MAP[26] = {
  0b11000000,0b11111001,0b10100100,0b10110000,0b10011001,
  0b10010010,0b10000010,0b11111000,0b10000000,0b10010000,
  0b10001000,0b10000011,0b11000110,0b10100001,0b10000110,
  0b10001110,0b10000010,0b10001001,0b11111001,0b11100001,
  0b11000111,0b11000000,0b10001100,0b10010010,0b11000001,
  0b11111111
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
  displayText("BOOTING");

  // ✅ Setup WiFi Manager (auto portal if not configured)
  WiFiManager wm;
  if (!wm.autoConnect("Kilipiboard Setup")) {
    Serial.println("❌ WiFi setup failed, restarting...");
    ESP.restart();
  }

  // ✅ Generate automatic unique device ID
  deviceID = "device_" + String(ESP.getChipId(), HEX);
  deviceID.toUpperCase();

  Serial.println("✅ Connected to WiFi!");
  Serial.print("Device ID: "); Serial.println(deviceID);
  Serial.print("IP Address: "); Serial.println(WiFi.localIP());

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
      Serial.println("📟 Updated display to: " + displayMessage);
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

  String serverHost = serverBase + deviceID;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  Serial.println("🌍 Fetching from: " + serverHost);
  if (!http.begin(client, serverHost)) {
    Serial.println("❌ HTTP begin failed");
    return "";
  }

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    payload.trim();
    Serial.println("✅ Message fetched: " + payload);
    http.end();
    return payload;
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
  for (int i = 0; i < len; i++) displayBuffer[i] = charToPattern(text[i]);
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
