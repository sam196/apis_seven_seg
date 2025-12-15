#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>

// ============================================
// PIN CONFIGURATION
// ============================================
#define DATA_PIN  D5 
#define CLOCK_PIN D7
#define LATCH_PIN D6

// ============================================
// WIFI CONFIGURATION - CHANGE THESE!
// ============================================
const char* ssid = "TP-Link_653C";
const char* password = "42578444";

// ============================================ 
// HTTP FETCH CONFIGURATION
// ============================================
const char* fetchURL = "https://1c511b7a7b7c.ngrok-free.app/display.txt";  // CHANGE THIS!
unsigned long fetchInterval = 10000;  // Fetch every 10 seconds
unsigned long lastFetchTime = 0;
bool autoFetchEnabled = true;

// ============================================
// DISPLAY CONFIGURATION
// ============================================
#define NUM_DIGITS 15
#define REFRESH_INTERVAL 100

const uint8_t DIGIT_MAP[NUM_DIGITS] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
};

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
  0b10001000, // 10: A
  0b10000011, // 11: b
  0b11000110, // 12: C
  0b10100001, // 13: d
  0b10000110, // 14: E
  0b10001110, // 15: F
  0b10000010, // 16: G
  0b10001001, // 17: H
  0b11111001, // 18: I
  0b11100001, // 19: J
  0b11000111, // 20: L
  0b11000000, // 21: O
  0b10001100, // 22: P
  0b10010010, // 23: S
  0b11000001, // 24: U
  0b11111111, // 25: blank
};

#define CHAR_BLANK 25
#define CHAR_MINUS 17

// ============================================
// GLOBAL VARIABLES
// ============================================
uint8_t displayBuffer[NUM_DIGITS];
uint8_t currentDigit = 0;
unsigned long lastRefresh = 0;
uint8_t brightness = 100;
String displayMessage = "HELLO WORLD";
bool scrollEnabled = false;
int scrollPosition = 0;
unsigned long lastScroll = 0;

ESP8266WebServer server(80);
WiFiClient wifiClient;

// ============================================
// FUNCTION PROTOTYPES
// ============================================
void updateDisplay();
void writeToRegisters(uint8_t segments, uint16_t digits);
void displayText(String text);
void clearDisplay();
uint8_t charToPattern(char c);
void displayScrollText();
void fetchDataFromURL();
void handleRoot();
void handleSetMessage();
void handleGetStatus();
void handleSetURL();
void handleFetchNow();

// ============================================
// SETUP
// ============================================
void setup() {
  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  
  digitalWrite(LATCH_PIN, LOW);
  digitalWrite(DATA_PIN, LOW);
  for (int i = 0; i < 24; i++) {
    digitalWrite(CLOCK_PIN, HIGH);
    delayMicroseconds(1);
    digitalWrite(CLOCK_PIN, LOW);
    delayMicroseconds(1);
  }
  digitalWrite(LATCH_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(LATCH_PIN, LOW);
  
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== SAMTRONICS LED Display with HTTP Fetch ===");
  
  clearDisplay();
  
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    updateDisplay();
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    server.on("/", handleRoot);
    server.on("/setMessage", HTTP_POST, handleSetMessage);
    server.on("/status", handleGetStatus);
    server.on("/setURL", HTTP_POST, handleSetURL);
    server.on("/fetchNow", handleFetchNow);
    
    server.begin();
    Serial.println("Web server started!");
    
    displayText("SAMTRONICS");
    delay(2000);
    displayText(WiFi.localIP().toString());
    delay(3000);
    displayText("HELLO");
  } else {
    Serial.println("\nWiFi Failed!");
    displayText("WIFI FAIL");
  }
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
  updateDisplay();
  server.handleClient();
  
  // Auto-fetch from URL
  if (autoFetchEnabled && millis() - lastFetchTime > fetchInterval) {
    fetchDataFromURL();
    lastFetchTime = millis();
  }
  
  // Handle scrolling
  if (scrollEnabled && displayMessage.length() > NUM_DIGITS) {
    if (millis() - lastScroll > 300) {
      lastScroll = millis();
      scrollPosition++;
      if (scrollPosition >= displayMessage.length() + 3) {
        scrollPosition = 0;
      }
      displayScrollText();
    }
  }
}

// ============================================
// HTTP FETCH FUNCTION
// ============================================
void fetchDataFromURL() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected!");
    return;
  }
  
  HTTPClient http;
  Serial.printf("Fetching from: %s\n", fetchURL);
  
  http.begin(wifiClient, fetchURL);
  http.setTimeout(5000);  // 5 second timeout
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    payload.trim();  // Remove whitespace
    
    if (payload.length() > 0) {
      Serial.printf("Fetched: %s\n", payload.c_str());
      displayMessage = payload;
      displayMessage.toUpperCase();
      
      scrollPosition = 0;
      
      if (scrollEnabled && displayMessage.length() > NUM_DIGITS) {
        displayScrollText();
      } else {
        displayText(displayMessage);
      }
    }
  } else {
    Serial.printf("HTTP GET failed: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
}

// ============================================
// DISPLAY FUNCTIONS (same as before)
// ============================================
void updateDisplay() {
  unsigned long now = micros();
  
  if (now - lastRefresh >= REFRESH_INTERVAL) {
    lastRefresh = now;
    
    uint8_t segmentIndex = displayBuffer[currentDigit];
    if (segmentIndex >= 26) {
      segmentIndex = CHAR_BLANK;
    }
    
    uint8_t segmentPattern = SEGMENT_MAP[segmentIndex];
    uint8_t physicalDigit = DIGIT_MAP[currentDigit];
    
    uint16_t digitMask = 0xFFFF;
    digitMask &= ~(1 << physicalDigit);
    
    writeToRegisters(segmentPattern, digitMask);
    
    if (brightness < 100) {
      uint16_t onTimeMicros = (REFRESH_INTERVAL * brightness) / 100;
      if (onTimeMicros > 10) {
        delayMicroseconds(onTimeMicros);
      }
      writeToRegisters(0xFF, 0xFFFF);
    }
    
    currentDigit = (currentDigit + 1) % NUM_DIGITS;
  }
}

void writeToRegisters(uint8_t segments, uint16_t digits) {
  digitalWrite(LATCH_PIN, LOW);
  delayMicroseconds(1);
  
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, (digits >> 8) & 0xFF);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, digits & 0xFF);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, segments);
  
  delayMicroseconds(1);
  digitalWrite(LATCH_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(LATCH_PIN, LOW);
}

void displayText(String text) {
  clearDisplay();
  text.toUpperCase();
  
  int len = min((int)text.length(), NUM_DIGITS);
  
  for (int i = 0; i < len; i++) {
    displayBuffer[i] = charToPattern(text[i]);
  }
  
  Serial.print("Display: ");
  Serial.println(text);
}

void displayScrollText() {
  clearDisplay();
  String paddedMessage = displayMessage + "   ";
  
  for (int i = 0; i < NUM_DIGITS; i++) {
    int textPos = (scrollPosition + i) % paddedMessage.length();
    displayBuffer[i] = charToPattern(paddedMessage[textPos]);
  }
}

void clearDisplay() {
  for (int i = 0; i < NUM_DIGITS; i++) {
    displayBuffer[i] = CHAR_BLANK;
  }
}

uint8_t charToPattern(char c) {
  if (c >= 'a' && c <= 'z') {
    c = c - 'a' + 'A';
  }
  
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  
  switch(c) {
    case 'A': return 10;
    case 'B': return 11;
    case 'C': return 12;
    case 'D': return 13;
    case 'E': return 14;
    case 'F': return 15;
    case 'G': return 16;
    case 'H': return 17;
    case 'I': return 18;
    case 'J': return 19;
    case 'L': return 20;
    case 'O': return 21;
    case 'P': return 22;
    case 'S': return 23;
    case 'U': return 24;
    case ' ': return CHAR_BLANK;
    case '-': return CHAR_MINUS;
    case '.': return CHAR_BLANK;
    default: return CHAR_BLANK;
  }
}

// ============================================
// WEB SERVER HANDLERS
// ============================================
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>SAMTRONICS LED Display - HTTP Fetch</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      padding: 20px;
    }
    .container {
      background: rgba(255,255,255,0.95);
      border-radius: 20px;
      padding: 40px;
      max-width: 800px;
      margin: 0 auto;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
    }
    .header {
      text-align: center;
      margin-bottom: 30px;
    }
    .brand {
      font-size: 36px;
      font-weight: bold;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
      letter-spacing: 3px;
    }
    .section {
      background: #f8f9fa;
      padding: 20px;
      border-radius: 15px;
      margin-bottom: 20px;
    }
    .section-title {
      font-size: 18px;
      font-weight: bold;
      margin-bottom: 15px;
      color: #333;
    }
    .form-group {
      margin-bottom: 20px;
    }
    label {
      display: block;
      margin-bottom: 8px;
      font-weight: 600;
      color: #333;
    }
    input[type="text"], input[type="number"] {
      width: 100%;
      padding: 12px 15px;
      border: 2px solid #ddd;
      border-radius: 10px;
      font-size: 16px;
    }
    input:focus {
      outline: none;
      border-color: #667eea;
    }
    button {
      padding: 12px 24px;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      border: none;
      border-radius: 10px;
      font-size: 16px;
      font-weight: bold;
      cursor: pointer;
      transition: transform 0.2s;
      margin-right: 10px;
    }
    button:hover {
      transform: translateY(-2px);
    }
    .btn-secondary {
      background: #6c757d;
    }
    .slider-container {
      display: flex;
      align-items: center;
      gap: 15px;
    }
    input[type="range"] {
      flex: 1;
      height: 8px;
      border-radius: 5px;
      background: #ddd;
      outline: none;
      -webkit-appearance: none;
    }
    input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 20px;
      height: 20px;
      border-radius: 50%;
      background: #667eea;
      cursor: pointer;
    }
    .checkbox-group {
      display: flex;
      align-items: center;
      gap: 10px;
    }
    input[type="checkbox"] {
      width: 20px;
      height: 20px;
      cursor: pointer;
    }
    .status {
      padding: 15px;
      background: #d4edda;
      border: 2px solid #c3e6cb;
      border-radius: 10px;
      text-align: center;
      color: #155724;
      font-weight: 600;
      margin-top: 20px;
    }
    .info-box {
      padding: 15px;
      background: #e7f3ff;
      border-left: 4px solid #667eea;
      border-radius: 5px;
      font-size: 13px;
      margin-top: 20px;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <div class="brand">SAMTRONICS</div>
      <div style="color: #666; font-size: 14px;">HTTP Fetch Edition</div>
    </div>
    
    <div class="section">
      <div class="section-title">🌐 Auto-Fetch from URL</div>
      
      <div class="form-group">
        <label>URL to fetch display text from:</label>
        <input type="text" id="fetchURL" placeholder="http://example.com/display.txt">
      </div>
      
      <div class="form-group">
        <label>Fetch interval (seconds):</label>
        <input type="number" id="fetchInterval" value="10" min="1" max="3600">
      </div>
      
      <div class="checkbox-group">
        <input type="checkbox" id="autoFetch" checked>
        <label for="autoFetch" style="margin: 0;">Enable auto-fetch</label>
      </div>
      
      <div style="margin-top: 15px;">
        <button onclick="updateFetchSettings()">💾 Save Settings</button>
        <button onclick="fetchNow()" class="btn-secondary">🔄 Fetch Now</button>
      </div>
    </div>
    
    <div class="section">
      <div class="section-title">📝 Manual Message</div>
      
      <div class="form-group">
        <label>Display Message:</label>
        <input type="text" id="message" placeholder="Enter your message...">
      </div>
      
      <div class="form-group">
        <label>💡 Brightness</label>
        <div class="slider-container">
          <input type="range" id="brightness" value="100" min="0" max="100" 
                 oninput="document.getElementById('brightnessValue').textContent = this.value">
          <span style="min-width: 45px; text-align: center; font-weight: bold; color: #667eea;">
            <span id="brightnessValue">100</span>%
          </span>
        </div>
      </div>
      
      <div class="checkbox-group">
        <input type="checkbox" id="scroll">
        <label for="scroll" style="margin: 0;">🔄 Enable scrolling</label>
      </div>
      
      <button onclick="sendMessage()">✨ Update Display</button>
    </div>
    
    <div class="status" id="status">HELLO</div>
    
    <div class="info-box">
      <strong>📖 How to use:</strong><br>
      • Set a URL that returns plain text (e.g., http://yourserver.com/display.txt)<br>
      • The display will automatically fetch and show the text at the specified interval<br>
      • You can also manually send messages using the form above<br>
      • The URL should return only the text you want to display (max 200 chars)
    </div>
  </div>

  <script>
    function updateFetchSettings() {
      const url = document.getElementById('fetchURL').value;
      const interval = document.getElementById('fetchInterval').value;
      const autoFetch = document.getElementById('autoFetch').checked;
      
      if (!url) {
        showStatus('⚠️ Please enter a URL', '#fff3cd');
        return;
      }
      
      const data = `url=${encodeURIComponent(url)}&interval=${interval}&auto=${autoFetch ? '1' : '0'}`;
      
      fetch('/setURL', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: data
      })
      .then(() => showStatus('✅ Fetch settings saved!', '#d4edda'))
      .catch(() => showStatus('❌ Connection error', '#f8d7da'));
    }
    
    function fetchNow() {
      fetch('/fetchNow')
        .then(() => showStatus('🔄 Fetching data...', '#d1ecf1'))
        .catch(() => showStatus('❌ Fetch failed', '#f8d7da'));
    }
    
    function sendMessage() {
      const message = document.getElementById('message').value;
      const brightness = document.getElementById('brightness').value;
      const scroll = document.getElementById('scroll').checked;
      
      if (!message) {
        showStatus('⚠️ Please enter a message', '#fff3cd');
        return;
      }
      
      const data = `message=${encodeURIComponent(message)}&brightness=${brightness}&scroll=${scroll ? '1' : '0'}`;
      
      fetch('/setMessage', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: data
      })
      .then(() => showStatus(`✅ Displaying: "${message}"`, '#d4edda'))
      .catch(() => showStatus('❌ Connection error', '#f8d7da'));
    }
    
    function showStatus(text, bg) {
      const status = document.getElementById('status');
      status.textContent = text;
      status.style.background = bg;
    }
  </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void handleSetMessage() {
  if (server.hasArg("message")) {
    displayMessage = server.arg("message");
    displayMessage.toUpperCase();
    
    scrollEnabled = server.hasArg("scroll") && server.arg("scroll") == "1";
    
    if (server.hasArg("brightness")) {
      brightness = constrain(server.arg("brightness").toInt(), 0, 100);
    }
    
    scrollPosition = 0;
    lastScroll = millis();
    
    if (scrollEnabled && displayMessage.length() > NUM_DIGITS) {
      displayScrollText();
    } else {
      displayText(displayMessage);
    }
    
    Serial.printf("Manual message: %s\n", displayMessage.c_str());
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing message");
  }
}

void handleSetURL() {
  if (server.hasArg("url")) {
    String newURL = server.arg("url");
    newURL.toCharArray((char*)fetchURL, 200);
    
    if (server.hasArg("interval")) {
      fetchInterval = server.arg("interval").toInt() * 1000;  // Convert to milliseconds
    }
    
    autoFetchEnabled = server.hasArg("auto") && server.arg("auto") == "1";
    
    Serial.printf("Fetch URL updated: %s\n", fetchURL);
    Serial.printf("Interval: %lu ms, Auto: %s\n", fetchInterval, autoFetchEnabled ? "ON" : "OFF");
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing URL");
  }
}

void handleFetchNow() {
  fetchDataFromURL();
  server.send(200, "text/plain", "Fetch triggered");
}

void handleGetStatus() {
  String json = "{";
  json += "\"message\":\"" + displayMessage + "\",";
  json += "\"brightness\":" + String(brightness) + ",";
  json += "\"scroll\":" + String(scrollEnabled ? "true" : "false") + ",";
  json += "\"fetchURL\":\"" + String(fetchURL) + "\",";
  json += "\"autoFetch\":" + String(autoFetchEnabled ? "true" : "false") + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}