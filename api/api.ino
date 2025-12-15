#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

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
// DISPLAY CONFIGURATION
// ============================================
#define NUM_DIGITS 15
#define REFRESH_INTERVAL 100

const uint8_t DIGIT_MAP[NUM_DIGITS] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
};

// ============================================
// SEGMENT PATTERNS (COMMON ANODE)
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

String apiEndpoint = "";
unsigned long lastApiFetch = 0;
unsigned long apiFetchInterval = 60000;
bool autoFetchEnabled = false;
String jsonPath = "";

ESP8266WebServer server(80);

// ============================================
// FUNCTION PROTOTYPES
// ============================================
void updateDisplay();
void writeToRegisters(uint8_t segments, uint16_t digits);
void displayText(String text);
void displayScrollText();
void clearDisplay();
uint8_t charToPattern(char c);
void handleRoot();
void handleSetMessage();
void handleGetStatus();
void handleTestDigits();
void handleFetchAPI();
void handleSetAPI();
String fetchDataFromAPI(String url);
String extractJsonValue(String json, String path);

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
  Serial.println("\n=== SAMTRONICS LED Display ===");
  
  clearDisplay();
  
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
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
    server.on("/testDigits", handleTestDigits);
    server.on("/fetchAPI", handleFetchAPI);
    server.on("/setAPI", HTTP_POST, handleSetAPI);
    
    server.begin();
    Serial.println("Web server started!");
    
    displayText("SAMTRONICS");
    delay(2000);
    displayText(WiFi.localIP().toString());
    delay(3000);
    displayText("READY");
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
  
  if (autoFetchEnabled && apiEndpoint.length() > 0) {
    if (millis() - lastApiFetch > apiFetchInterval) {
      lastApiFetch = millis();
      String data = fetchDataFromAPI(apiEndpoint);
      if (data.length() > 0) {
        if (jsonPath.length() > 0) {
          data = extractJsonValue(data, jsonPath);
        }
        displayMessage = data;
        if (scrollEnabled && displayMessage.length() > NUM_DIGITS) {
          displayScrollText();
        } else {
          displayText(displayMessage);
        }
        Serial.println("Auto-fetched: " + data);
      }
    }
  }
}

// ============================================
// DISPLAY FUNCTIONS
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
// API FUNCTIONS
// ============================================
String fetchDataFromAPI(String url) {
  WiFiClient client;
  HTTPClient http;
  
  Serial.println("Fetching: " + url);
  
  http.begin(client, url);
  http.setTimeout(5000);
  
  int httpCode = http.GET();
  String payload = "";
  
  if (httpCode == HTTP_CODE_OK) {
    payload = http.getString();
    Serial.println("Fetch successful!");
  } else {
    Serial.printf("Fetch failed: %d\n", httpCode);
  }
  
  http.end();
  return payload;
}

String extractJsonValue(String json, String path) {
  if (path.length() == 0) return json;
  
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    Serial.println("JSON parse failed");
    return json;
  }
  
  String result = "";
  
  if (path.indexOf('.') > 0) {
    int dotPos = path.indexOf('.');
    String key1 = path.substring(0, dotPos);
    String key2 = path.substring(dotPos + 1);
    
    if (doc.containsKey(key1) && doc[key1].containsKey(key2)) {
      result = doc[key1][key2].as<String>();
    }
  } else {
    if (doc.containsKey(path)) {
      result = doc[path].as<String>();
    }
  }
  
  return result.length() > 0 ? result : json;
}

// ============================================
// WEB HANDLERS
// ============================================
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>SAMTRONICS LED Display</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 20px;
    }
    .container {
      background: rgba(255,255,255,0.95);
      border-radius: 20px;
      padding: 40px;
      max-width: 600px;
      width: 100%;
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
    .version {
      color: #666;
      font-size: 12px;
      margin-top: 5px;
    }
    .form-group {
      margin-bottom: 25px;
    }
    label {
      display: block;
      margin-bottom: 8px;
      font-weight: 600;
      color: #333;
    }
    input[type="text"] {
      width: 100%;
      padding: 12px 15px;
      border: 2px solid #ddd;
      border-radius: 10px;
      font-size: 16px;
      transition: border 0.3s;
    }
    input[type="text"]:focus {
      outline: none;
      border-color: #667eea;
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
    .brightness-value {
      min-width: 45px;
      text-align: center;
      font-weight: bold;
      color: #667eea;
    }
    .checkbox-group {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 15px;
      background: #f8f9fa;
      border-radius: 10px;
    }
    input[type="checkbox"] {
      width: 20px;
      height: 20px;
      cursor: pointer;
    }
    button {
      width: 100%;
      padding: 15px;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
      border: none;
      border-radius: 10px;
      font-size: 18px;
      font-weight: bold;
      cursor: pointer;
      transition: transform 0.2s, box-shadow 0.2s;
      box-shadow: 0 4px 15px rgba(102, 126, 234, 0.4);
    }
    button:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 20px rgba(102, 126, 234, 0.6);
    }
    button:active {
      transform: translateY(0);
    }
    .test-grid {
      display: grid;
      grid-template-columns: repeat(5, 1fr);
      gap: 8px;
      margin-top: 15px;
    }
    .test-btn {
      padding: 12px;
      font-size: 14px;
      background: #f8f9fa;
      color: #333;
      border: 2px solid #ddd;
    }
    .test-btn:hover {
      background: #667eea;
      color: white;
      border-color: #667eea;
    }
    .status {
      margin-top: 20px;
      padding: 15px;
      background: #d4edda;
      border: 2px solid #c3e6cb;
      border-radius: 10px;
      text-align: center;
      color: #155724;
      font-weight: 600;
    }
    .info-box {
      margin-top: 20px;
      padding: 15px;
      background: #e7f3ff;
      border-left: 4px solid #667eea;
      border-radius: 5px;
      font-size: 13px;
      color: #333;
      line-height: 1.6;
    }
    .api-section {
      margin-top: 30px;
      padding-top: 20px;
      border-top: 2px solid rgba(102, 126, 234, 0.3);
    }
    h3 {
      color: #333;
      margin-bottom: 15px;
    }
    .btn-green {
      background: linear-gradient(135deg, #11998e 0%, #38ef7d 100%);
      margin-top: 10px;
    }
    .btn-pink {
      background: linear-gradient(135deg, #f093fb 0%, #f5576c 100%);
      margin-top: 10px;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <div class="brand">SAMTRONICS</div>
      <div class="version">v2.0 - API Edition</div>
    </div>
    
    <div class="form-group">
      <label>📝 Display Message</label>
      <input type="text" id="message" placeholder="Enter your message..." maxlength="200">
    </div>
    
    <div class="form-group">
      <label>💡 Brightness</label>
      <div class="slider-container">
        <input type="range" id="brightness" value="100" min="0" max="100" oninput="updateBrightnessDisplay()">
        <span class="brightness-value"><span id="brightnessValue">100</span>%</span>
      </div>
    </div>
    
    <div class="checkbox-group">
      <input type="checkbox" id="scroll">
      <label for="scroll" style="margin: 0;">🔄 Enable scrolling for long messages</label>
    </div>
    
    <button onclick="sendMessage()">✨ Update Display</button>
    
    <div class="form-group" style="margin-top: 30px;">
      <label>🧪 Quick Test Patterns</label>
      <div class="test-grid">
        <button class="test-btn" onclick="testPattern('0123456789')">Numbers</button>
        <button class="test-btn" onclick="testPattern('ABCDEFGHIJL')">Letters</button>
        <button class="test-btn" onclick="testPattern('HELLO')">Hello</button>
        <button class="test-btn" onclick="testPattern('88888888888888')">All 8s</button>
        <button class="test-btn" onclick="testPattern('---------------')">Dashes</button>
      </div>
    </div>
    
    <div class="api-section">
      <h3>🌐 Fetch from API</h3>
      
      <div class="form-group">
        <label>API Endpoint URL</label>
        <input type="text" id="apiUrl" placeholder="https://api.example.com/data">
      </div>
      
      <div class="form-group">
        <label>JSON Path (optional)</label>
        <input type="text" id="jsonPath" placeholder="e.g. data.temperature">
      </div>
      
      <div class="form-group">
        <label>Auto-fetch Interval (seconds)</label>
        <div class="slider-container">
          <input type="range" id="fetchInterval" value="60" min="5" max="300" step="5" oninput="updateIntervalDisplay()">
          <span class="brightness-value"><span id="intervalValue">60</span>s</span>
        </div>
      </div>
      
      <div class="checkbox-group">
        <input type="checkbox" id="autoFetch">
        <label for="autoFetch" style="margin: 0;">🔄 Enable auto-fetch</label>
      </div>
      
      <button class="btn-green" onclick="fetchNow()">📥 Fetch Now</button>
      <button class="btn-pink" onclick="saveAPISettings()">💾 Save API Settings</button>
    </div>
    
    <div class="status" id="status">Ready to display</div>
    
    <div class="info-box">
      <strong>ℹ️ Display Info:</strong><br>
      • 15 character display<br>
      • Supports: 0-9, A-F, G-P, S, U, H, J, L, O<br>
      • Enable scroll for messages over 15 characters
    </div>
  </div>

  <script>
    function updateBrightnessDisplay() {
      document.getElementById('brightnessValue').textContent = 
        document.getElementById('brightness').value;
    }
    
    function updateIntervalDisplay() {
      document.getElementById('intervalValue').textContent = 
        document.getElementById('fetchInterval').value;
    }
    
    function sendMessage() {
      const message = document.getElementById('message').value;
      const brightness = document.getElementById('brightness').value;
      const scroll = document.getElementById('scroll').checked;
      
      if (!message) {
        showStatus('⚠️ Please enter a message', '#fff3cd', '#856404');
        return;
      }
      
      const data = `message=${encodeURIComponent(message)}&brightness=${brightness}&scroll=${scroll ? '1' : '0'}`;
      
      fetch('/setMessage', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: data
      })
      .then(response => response.text())
      .then(() => {
        showStatus(`✅ Displaying: "${message}"`, '#d4edda', '#155724');
      })
      .catch(error => {
        showStatus('❌ Connection error', '#f8d7da', '#721c24');
      });
    }
    
    function fetchNow() {
      const apiUrl = document.getElementById('apiUrl').value;
      const jsonPath = document.getElementById('jsonPath').value;
      
      if (!apiUrl) {
        showStatus('⚠️ Please enter API URL', '#fff3cd', '#856404');
        return;
      }
      
      showStatus('⏳ Fetching data...', '#d1ecf1', '#0c5460');
      
      fetch('/fetchAPI?url=' + encodeURIComponent(apiUrl) + '&path=' + encodeURIComponent(jsonPath))
        .then(response => response.text())
        .then(data => {
          showStatus('✅ Fetched: ' + data, '#d4edda', '#155724');
        })
        .catch(error => {
          showStatus('❌ Fetch failed', '#f8d7da', '#721c24');
        });
    }
    
    function saveAPISettings() {
      const apiUrl = document.getElementById('apiUrl').value;
      const jsonPath = document.getElementById('jsonPath').value;
      const interval = document.getElementById('fetchInterval').value;
      const autoFetch = document.getElementById('autoFetch').checked;
      
      if (!apiUrl) {
        showStatus('⚠️ Please enter API URL', '#fff3cd', '#856404');
        return;
      }
      
      const data = `url=${encodeURIComponent(apiUrl)}&path=${encodeURIComponent(jsonPath)}&interval=${interval}&auto=${autoFetch ? '1' : '0'}`;
      
      fetch('/setAPI', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: data
      })
      .then(response => response.text())
      .then(() => {
        showStatus('✅ API settings saved', '#d4edda', '#155724');
      })
      .catch(error => {
        showStatus('❌ Failed to save settings', '#f8d7da', '#721c24');
      });
    }
    
    function testPattern(pattern) {
      document.getElementById('message').value = pattern;
      sendMessage();
    }
    
    function showStatus(text, bg, color) {
      const status = document.getElementById('status');
      status.textContent = text;
      status.style.background = bg;
      status.style.color = color;
      status.style.borderColor = color;
    }
    
    document.getElementById('message').addEventListener('keypress', (e) => {
      if (e.key === 'Enter') sendMessage();
    });
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
    
    Serial.printf("Message: %s | Brightness: %d%% | Scroll: %s\n", 
                  displayMessage.c_str(), brightness, scrollEnabled ? "ON" : "OFF");
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing message");
  }
}

void handleGetStatus() {
  String json = "{";
  json += "\"message\":\"" + displayMessage + "\",";
  json += "\"brightness\":" + String(brightness) + ",";
  json += "\"scroll\":" + String(scrollEnabled ? "true" : "false") + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"apiEndpoint\":\"" + apiEndpoint + "\",";
  json += "\"autoFetch\":" + String(autoFetchEnabled ? "true" : "false");
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleTestDigits() {
  if (server.hasArg("bit")) {
    int bit = server.arg("bit").toInt();
    clearDisplay();
    
    for (int i = 0; i < NUM_DIGITS; i++) {
      displayBuffer[i] = 8;
    }
    
    uint16_t digitMask = 0xFFFF;
    digitMask &= ~(1 << bit);
    
    for (int i = 0; i < 100; i++) {
      writeToRegisters(SEGMENT_MAP[8], digitMask);
      delayMicroseconds(1000);
    }
    
    server.send(200, "text/plain", "Tested bit " + String(bit));
    clearDisplay();
    displayText(displayMessage);
  } else {
    String html = "<html><body><h2>Digit Bit Tester</h2>";
    html += "<p>Click each bit to see which physical digit lights up:</p>";
    for (int i = 0; i < 16; i++) {
      html += "<button onclick=\"testBit(" + String(i) + ")\">Test Bit " + String(i) + "</button> ";
    }
    html += "<p id='result'></p>";
    html += "<script>";
    html += "function testBit(bit) {";
    html += "  fetch('/testDigits?bit=' + bit).then(r => r.text()).then(t => {";
    html += "    document.getElementById('result').innerText = 'Bit ' + bit + ' tested - which digit lit up?';";
    html += "  });";
    html += "}";
    html += "</script></body></html>";
    server.send(200, "text/html", html);
  }
}

void handleFetchAPI() {
  if (server.hasArg("url")) {
    String url = server.arg("url");
    String path = server.hasArg("path") ? server.arg("path") : "";
    
    String data = fetchDataFromAPI(url);
    
    if (data.length() > 0) {
      if (path.length() > 0) {
        data = extractJsonValue(data, path);
      }
      
      displayMessage = data;
      
      if (scrollEnabled && displayMessage.length() > NUM_DIGITS) {
        displayScrollText();
      } else {
        displayText(displayMessage);
      }
      
      server.send(200, "text/plain", data);
    } else {
      server.send(500, "text/plain", "Failed to fetch data");
    }
  } else {
    server.send(400, "text/plain", "Missing URL parameter");
  }
}

void handleSetAPI() {
  if (server.hasArg("url")) {
    apiEndpoint = server.arg("url");
    jsonPath = server.hasArg("path") ? server.arg("path") : "";
    
    if (server.hasArg("interval")) {
      apiFetchInterval = server.arg("interval").toInt() * 1000;
    }
    
    autoFetchEnabled = server.hasArg("auto") && server.arg("auto") == "1";
    lastApiFetch = millis();
    
    Serial.printf("API configured: %s | Path: %s | Interval: %lus | Auto: %s\n",
                  apiEndpoint.c_str(), jsonPath.c_str(), 
                  apiFetchInterval/1000, autoFetchEnabled ? "ON" : "OFF");
    
    server.send(200, "text/plain", "API settings saved");
  } else {
    server.send(400, "text/plain", "Missing URL parameter");
  }
}
