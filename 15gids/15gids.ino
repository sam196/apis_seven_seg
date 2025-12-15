#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

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
#define REFRESH_INTERVAL 100  // Microseconds per digit (100us * 15 = 1.5ms total cycle)

// DIGIT MAPPING - Maps logical display position (0-14) to physical shift register bit
// Standard sequential mapping - adjust if your hardware is wired differently
const uint8_t DIGIT_MAP[NUM_DIGITS] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14  // Bits 0-14 for 15 digits
};

// ============================================
// SEGMENT PATTERNS (COMMON ANODE - inverted)
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
  0b10000010, // 16: G (same as 6)
  0b10001001, // 17: H
  0b11111001, // 18: I (same as 1)
  0b11100001, // 19: J
  0b11000111, // 20: L
  0b11000000, // 21: O (same as 0)
  0b10001100, // 22: P
  0b10010010, // 23: S (same as 5)
  0b11000001, // 24: U
  0b11111111, // 25: blank
};

#define CHAR_BLANK 25
#define CHAR_MINUS 17  // Use H pattern for minus (not ideal but visible)

// ============================================
// GLOBAL VARIABLES
// ============================================
uint8_t displayBuffer[NUM_DIGITS];
uint8_t currentDigit = 0;
unsigned long lastRefresh = 0;
uint8_t brightness = 100;  // 0-100%
String displayMessage = "HELLO WORLD";
bool scrollEnabled = false;
int scrollPosition = 0;
unsigned long lastScroll = 0;
uint16_t onTime = 100;  // Microseconds on-time per digit (for brightness)

ESP8266WebServer server(80);

// ============================================
// FUNCTION PROTOTYPES
// ============================================
void updateDisplay();
void writeToRegisters(uint8_t segments, uint16_t digits);
void displayText(String text);
void clearDisplay();
uint8_t charToPattern(char c);
void handleRoot();
void handleSetMessage();
void handleGetStatus();
void updateBrightness();

// ============================================
// SETUP
// ============================================
void setup() {
  // Initialize pins
  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  
  // Clear shift registers
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
  
  // Initialize serial
  Serial.begin(115200);
  delay(100);
  Serial.println("\n=== SAMTRONICS LED Display ===");
  
  clearDisplay();
  updateBrightness();
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    updateDisplay();  // Keep display alive during connection
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Setup web server routes
    server.on("/", handleRoot);
    server.on("/setMessage", HTTP_POST, handleSetMessage);
    server.on("/status", handleGetStatus);
    server.on("/testDigits", handleTestDigits);
    
    server.begin();
    Serial.println("Web server started!");
    
    // Startup sequence
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
  
  // Handle scrolling (check every 300ms)
  if (scrollEnabled && displayMessage.length() > NUM_DIGITS) {
    if (millis() - lastScroll > 300) {
      lastScroll = millis();
      scrollPosition++;
      // Add padding spaces for smooth loop
      if (scrollPosition >= displayMessage.length() + 3) {
        scrollPosition = 0;
      }
      displayScrollText();
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
    
    // Get segment pattern for current digit
    uint8_t segmentIndex = displayBuffer[currentDigit];
    
    // Bounds check
    if (segmentIndex >= 26) {
      segmentIndex = CHAR_BLANK;
    }
    
    uint8_t segmentPattern = SEGMENT_MAP[segmentIndex];
    
    // Map logical digit to physical shift register bit
    uint8_t physicalDigit = DIGIT_MAP[currentDigit];
    
    // Create digit mask for common anode (active LOW)
    uint16_t digitMask = 0xFFFF;  // Start with all HIGH (off)
    digitMask &= ~(1 << physicalDigit);  // Set physical digit bit LOW (on)
    
    writeToRegisters(segmentPattern, digitMask);
    
    // Brightness control via duty cycle
    // Calculate on-time based on brightness (0-100%)
    if (brightness < 100) {
      uint16_t onTimeMicros = (REFRESH_INTERVAL * brightness) / 100;
      if (onTimeMicros > 10) {  // Minimum 10us on-time
        delayMicroseconds(onTimeMicros);
      }
      // Turn off display for the remainder of the refresh period
      writeToRegisters(0xFF, 0xFFFF);  // All segments off, all digits off
    }
    
    currentDigit = (currentDigit + 1) % NUM_DIGITS;
  }
}

void writeToRegisters(uint8_t segments, uint16_t digits) {
  digitalWrite(LATCH_PIN, LOW);
  delayMicroseconds(1);
  
  // Send data: IC3 (high digit byte), IC2 (low digit byte), IC1 (segments)
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
  String paddedMessage = displayMessage + "   ";  // Add spacing between loops
  
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
  // Convert to uppercase
  if (c >= 'a' && c <= 'z') {
    c = c - 'a' + 'A';
  }
  
  // Numbers
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  
  // Letters
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
    case '.': return CHAR_BLANK;  // Can't display dot easily
    default: return CHAR_BLANK;
  }
}

void updateBrightness() {
  // Calculate on-time for PWM brightness (0-100%)
  onTime = map(brightness, 0, 100, 0, REFRESH_INTERVAL);
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
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <div class="brand">SAMTRONICS</div>
      <div class="version">v1.2 - Fixed Edition</div>
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
      updateBrightness();
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
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleTestDigits() {
  // Test each digit individually to find which ones work
  if (server.hasArg("bit")) {
    int bit = server.arg("bit").toInt();
    clearDisplay();
    
    // Light up all segments on the specified bit position
    for (int i = 0; i < NUM_DIGITS; i++) {
      displayBuffer[i] = 8;  // Set all to show "8"
    }
    
    // Override the multiplexing to show only one specific bit
    uint16_t digitMask = 0xFFFF;
    digitMask &= ~(1 << bit);
    
    for (int i = 0; i < 100; i++) {  // Show for ~100ms
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
