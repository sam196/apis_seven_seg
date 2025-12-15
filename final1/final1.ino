#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

#define DATA_PIN  D5
#define CLOCK_PIN D7
#define LATCH_PIN D6

const char* ssid = "TP-Link_653C";
const char* password = "42578444";

// ⚠️ Give each device a unique ID
const char* deviceID = "device1"; // Change to "device2" on the other gadget
const char* serverHost = "https://sevensegment.onrender.com/message";

#define NUM_DIGITS 15
#define REFRESH_INTERVAL 100
#define CHAR_BLANK 25

const uint8_t SEGMENT_MAP[26] = {
  0b11000000,0b11111001,0b10100100,0b10110000,0b10011001,
  0b10010010,0b10000010,0b11111000,0b10000000,0b10010000,
  0b10001000,0b10000011,0b11000110,0b10100001,0b10000110,
  0b10001110,0b10000010,0b10001001,0b11111001,0b11100001,
  0b11000111,0b11000000,0b10001100,0b10010010,0b11000001,
  0b11111111
};

uint8_t displayBuffer[NUM_DIGITS];
String displayMessage = "STARTING";
unsigned long lastFetch = 0;
unsigned long fetchInterval = 10000;
unsigned long lastRefresh = 0;
uint8_t currentDigit = 0;

void writeToRegisters(uint8_t segments, uint16_t digits) {
  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, (digits >> 8) & 0xFF);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, digits & 0xFF);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, segments);
  digitalWrite(LATCH_PIN, HIGH);
}

uint8_t charToPattern(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'Z') return (c - 'A') + 10;
  return 25;
}

void clearDisplay() {
  for (int i = 0; i < NUM_DIGITS; i++) displayBuffer[i] = 25;
}

void displayText(String text) {
  clearDisplay();
  text.toUpperCase();
  int len = min((int)text.length(), NUM_DIGITS);
  for (int i = 0; i < len; i++) displayBuffer[i] = charToPattern(text[i]);
  Serial.println("Display: " + text);
}

void updateDisplay() {
  unsigned long now = micros();
  if (now - lastRefresh < REFRESH_INTERVAL) return;
  lastRefresh = now;
  uint8_t seg = SEGMENT_MAP[displayBuffer[currentDigit]];
  uint16_t digitMask = ~(1 << currentDigit);
  writeToRegisters(seg, digitMask);
  currentDigit = (currentDigit + 1) % NUM_DIGITS;
}

String fetchMessage() {
  if (WiFi.status() != WL_CONNECTED) return "";

  WiFiClient client;
  HTTPClient http;
  String url = String(serverHost) + "?id=" + deviceID;
  if (!http.begin(client, url)) return "";

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

void setup() {
  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\n✅ Connected! IP: " + WiFi.localIP().toString());
  displayText("READY");
}

void loop() {
  updateDisplay();
  if (millis() - lastFetch > fetchInterval) {
    lastFetch = millis();
    String msg = fetchMessage();
    if (msg.length() > 0 && msg != displayMessage) {
      displayMessage = msg;
      displayText(msg);
    }
  }
}
