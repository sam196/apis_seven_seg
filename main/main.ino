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
#define NUM_DIGITS 16  // 16-digit display
#define REFRESH_INTERVAL 1

// ============================================
// SEGMENT PATTERNS (COMMON ANODE)
// ============================================
const uint8_t SEGMENT_MAP[21] = {
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
  0b11111111, // blank
  0b10111111, // minus
  0b10011100, // degree
  0b10001001, // H
  0b11000111  // L
};

#define CHAR_BLANK 16
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
bool displayOn = true;  // Power state

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
void handlePower();

// ============================================
// SETUP
// ============================================
void setup() {
  // Initialize pins
  pinMode(DATA_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(LATCH_PIN, OUTPUT);
  
  // Clear shift registers (3 shift registers)
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
  Serial.println("\n=== Web-Controlled LED Display ===");
  
  clearDisplay();
  writeToRegisters(0xFF, 0xFFFF);
  delay(100);
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Open browser to: http://");
    Serial.println(WiFi.localIP());
    
    // Setup web server routes
    server.on("/", handleRoot);
    server.on("/setMessage", HTTP_POST, handleSetMessage);
    server.on("/status", handleGetStatus);
    server.on("/power", HTTP_POST, handlePower);
    
    server.begin();
    Serial.println("Web server started!");
    
    // Startup sequence
    displayText("SAMTRONICS");
    delay(2000);
    displayText("VER1");
    delay(2000);
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
  if (displayOn) {
    updateDisplay();
    
    // Handle scrolling
    if (scrollEnabled && displayMessage.length() > NUM_DIGITS) {
      if (millis() - lastScroll > 300) {
        lastScroll = millis();
        scrollPosition++;
        if (scrollPosition > displayMessage.length()) {
          scrollPosition = 0;
        }
        displayScrollText();
      }
    }
  } else {
    // Turn off all displays when power is off
    writeToRegisters(0xFF, 0xFFFF);
  }
  
  server.handleClient();
}

// ============================================
// DISPLAY FUNCTIONS
// ============================================

void updateDisplay() {
  unsigned long now = micros();
  
  if (now - lastRefresh >= REFRESH_INTERVAL * 1000UL) {
    lastRefresh = now;
    
    uint8_t physicalDigit = currentDigit;
    uint8_t segments = displayBuffer[currentDigit];
    
    // Bounds check
    if (segments >= 21) {
      segments = CHAR_BLANK;
    }
    
    uint8_t segmentPattern = SEGMENT_MAP[segments];
    
    // FIXED: Use unsigned long for bit shift to handle bit 14 and 15 correctly
    uint16_t digitMask = ~(1UL << physicalDigit);
    
    // Debug output for D14 and D15
    if (physicalDigit == 14 || physicalDigit == 15) {
      Serial.print("D");
      Serial.print(physicalDigit);
      Serial.print(" mask: 0x");
      Serial.print(digitMask, HEX);
      Serial.print(" segment: 0x");
      Serial.println(segmentPattern, HEX);
    }
    
    writeToRegisters(segmentPattern, digitMask);
    currentDigit = (currentDigit + 1) % NUM_DIGITS;
  }
}

void writeToRegisters(uint8_t segments, uint16_t digits) {
  // Turn off latch to start data transfer
  digitalWrite(LATCH_PIN, LOW);
  delayMicroseconds(1);
  
  // Send data in order: IC3 (high byte digits), IC2 (low byte digits), IC1 (segments)
  // Send digit select data for IC3 (bits 8-15 of digits)
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, (digits >> 8) & 0xFF);
  
  // Send digit select data for IC2 (bits 0-7 of digits)
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, digits & 0xFF);
  
  // Send segment data for IC1
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, segments);
  
  // Latch the data
  delayMicroseconds(1);
  digitalWrite(LATCH_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(LATCH_PIN, LOW);
}

void displayText(String text) {
  clearDisplay();
  
  int len = text.length();
  if (len > NUM_DIGITS) len = NUM_DIGITS;
  
  for (int i = 0; i < len; i++) {
    displayBuffer[i] = charToPattern(text[i]);
  }
  
  // Debug output
  Serial.print("Displaying: ");
  Serial.print(text);
  Serial.print(" (");
  for (int i = 0; i < len; i++) {
    Serial.print(displayBuffer[i]);
    Serial.print(" ");
  }
  Serial.println(")");
}

void displayScrollText() {
  clearDisplay();
  
  for (int i = 0; i < NUM_DIGITS; i++) {
    int textPos = scrollPosition + i;
    if (textPos < displayMessage.length()) {
      displayBuffer[i] = charToPattern(displayMessage[textPos]);
    }
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
    case 'H': return 19;
    case 'L': return 20;
    case 'O': return 0;
    case ' ': return CHAR_BLANK;
    case '-': return CHAR_MINUS;
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
  <title>SAMTRONICS LED Display Control</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      max-width: 600px;
      margin: 50px auto;
      padding: 20px;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      color: white;
    }
    .container {
      background: rgba(255,255,255,0.1);
      border-radius: 15px;
      padding: 30px;
      backdrop-filter: blur(10px);
      box-shadow: 0 8px 32px 0 rgba(31, 38, 135, 0.37);
    }
    .brand {
      text-align: center;
      font-size: 32px;
      font-weight: bold;
      margin-bottom: 5px;
      text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
      letter-spacing: 2px;
    }
    .version {
      text-align: center;
      font-size: 14px;
      margin-bottom: 30px;
      opacity: 0.8;
    }
    h1 {
      text-align: center;
      margin-bottom: 30px;
      font-size: 24px;
      text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
    }
    .power-control {
      text-align: center;
      margin-bottom: 30px;
    }
    .power-btn {
      width: 200px;
      padding: 20px;
      font-size: 20px;
      border-radius: 50px;
      margin: 0 auto;
      transition: all 0.3s;
    }
    .power-on {
      background: #4CAF50;
    }
    .power-off {
      background: #f44336;
    }
    .form-group {
      margin-bottom: 20px;
    }
    label {
      display: block;
      margin-bottom: 8px;
      font-weight: bold;
      font-size: 14px;
    }
    input[type="text"], input[type="number"] {
      width: 100%;
      padding: 12px;
      border: none;
      border-radius: 8px;
      font-size: 16px;
      box-sizing: border-box;
      background: rgba(255,255,255,0.9);
    }
    .checkbox-group {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 10px 0;
    }
    input[type="checkbox"] {
      width: 20px;
      height: 20px;
      cursor: pointer;
    }
    button {
      width: 100%;
      padding: 15px;
      background: #4CAF50;
      color: white;
      border: none;
      border-radius: 8px;
      font-size: 18px;
      font-weight: bold;
      cursor: pointer;
      transition: all 0.3s;
      box-shadow: 0 4px 6px rgba(0,0,0,0.2);
    }
    button:hover {
      background: #45a049;
      transform: translateY(-2px);
      box-shadow: 0 6px 8px rgba(0,0,0,0.3);
    }
    button:active {
      transform: translateY(0);
    }
    .test-buttons {
      display: grid;
      grid-template-columns: repeat(5, 1fr);
      gap: 10px;
      margin-top: 20px;
    }
    .test-btn {
      padding: 10px;
      font-size: 16px;
      background: rgba(255,255,255,0.2);
    }
    .status {
      margin-top: 20px;
      padding: 15px;
      background: rgba(255,255,255,0.2);
      border-radius: 8px;
      text-align: center;
      font-weight: bold;
    }
    .info {
      margin-top: 20px;
      padding: 15px;
      background: rgba(255,255,255,0.1);
      border-radius: 8px;
      font-size: 12px;
      line-height: 1.6;
    }
    .footer {
      text-align: center;
      margin-top: 30px;
      padding-top: 20px;
      border-top: 1px solid rgba(255,255,255,0.2);
      font-size: 12px;
      opacity: 0.7;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="brand">SAMTRONICS</div>
    <div class="version">Version 1.3 - 16 Digits ✓</div>
    <h1>LED Display Control Panel</h1>
    
    <div class="power-control">
      <button id="powerBtn" class="power-btn power-on" onclick="togglePower()">
        🔆 DISPLAY ON
      </button>
    </div>
    
    <div class="form-group">
      <label for="message">Message:</label>
      <input type="text" id="message" placeholder="Enter text to display" maxlength="100">
    </div>
    
    <div class="form-group">
      <label for="brightness">Brightness (0-100%):</label>
      <input type="number" id="brightness" value="100" min="0" max="100">
    </div>
    
    <div class="checkbox-group">
      <input type="checkbox" id="scroll">
      <label for="scroll">Enable Scrolling (for long messages)</label>
    </div>
    
    <button onclick="sendMessage()">Update Display</button>
    
    <div class="form-group">
      <label>Test All Digits (0-9):</label>
      <div class="test-buttons">
        <button class="test-btn" onclick="testDigit(0)">0</button>
        <button class="test-btn" onclick="testDigit(1)">1</button>
        <button class="test-btn" onclick="testDigit(2)">2</button>
        <button class="test-btn" onclick="testDigit(3)">3</button>
        <button class="test-btn" onclick="testDigit(4)">4</button>
        <button class="test-btn" onclick="testDigit(5)">5</button>
        <button class="test-btn" onclick="testDigit(6)">6</button>
        <button class="test-btn" onclick="testDigit(7)">7</button>
        <button class="test-btn" onclick="testDigit(8)">8</button>
        <button class="test-btn" onclick="testDigit(9)">9</button>
      </div>
    </div>
    
    <div class="status" id="status">Ready - 16 Digits Active</div>
    
    <div class="info">
      <strong>Supported Characters:</strong><br>
      Numbers: 0-9<br>
      Letters: A-F, H, L, O (limited by 7-segment)<br>
      Special: Space, Dash (-)<br>
      <br>
      Display shows first 16 characters (D0-D15). Enable scrolling for longer messages.
    </div>
    
    <div class="footer">
      SAMTRONICS LED Display System &copy; 2024<br>
      16-Digit Web-Controlled Display
    </div>
  </div>

  <script>
    let displayPower = true;
    
    function togglePower() {
      displayPower = !displayPower;
      const btn = document.getElementById('powerBtn');
      
      fetch('/power', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: 'state=' + (displayPower ? '1' : '0')
      })
      .then(response => response.text())
      .then(data => {
        if (displayPower) {
          btn.className = 'power-btn power-on';
          btn.innerHTML = '🔆 DISPLAY ON';
          document.getElementById('status').innerText = 'Display is ON - 16 Digits Active';
        } else {
          btn.className = 'power-btn power-off';
          btn.innerHTML = '🌙 DISPLAY OFF';
          document.getElementById('status').innerText = 'Display is OFF';
        }
      })
      .catch(error => {
        document.getElementById('status').innerText = 'Error: ' + error;
      });
    }
    
    function sendMessage() {
      const message = document.getElementById('message').value;
      const brightness = document.getElementById('brightness').value;
      const scroll = document.getElementById('scroll').checked;
      
      if (!message) {
        document.getElementById('status').innerText = 'Please enter a message!';
        return;
      }
      
      const data = 'message=' + encodeURIComponent(message) + 
                   '&brightness=' + brightness + 
                   '&scroll=' + (scroll ? '1' : '0');
      
      fetch('/setMessage', {
        method: 'POST',
        headers: {'Content-Type': 'application/x-www-form-urlencoded'},
        body: data
      })
      .then(response => response.text())
      .then(data => {
        document.getElementById('status').innerText = 'Updated: "' + message + '"';
      })
      .catch(error => {
        document.getElementById('status').innerText = 'Error: ' + error;
      });
    }
    
    function testDigit(digit) {
      const testMsg = String(digit).repeat(16);
      document.getElementById('message').value = testMsg;
      sendMessage();
    }
    
    // Allow Enter key to submit
    document.getElementById('message').addEventListener('keypress', function(e) {
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
    scrollEnabled = server.hasArg("scroll") && server.arg("scroll") == "1";
    
    if (server.hasArg("brightness")) {
      brightness = server.arg("brightness").toInt();
      if (brightness > 100) brightness = 100;
      if (brightness < 0) brightness = 0;
    }
    
    scrollPosition = 0;
    
    if (scrollEnabled && displayMessage.length() > NUM_DIGITS) {
      // Will scroll in main loop
    } else {
      displayText(displayMessage);
    }
    
    Serial.print("Message updated: ");
    Serial.println(displayMessage);
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing message parameter");
  }
}

void handleGetStatus() {
  String json = "{";
  json += "\"message\":\"" + displayMessage + "\",";
  json += "\"brightness\":" + String(brightness) + ",";
  json += "\"scroll\":" + String(scrollEnabled ? "true" : "false") + ",";
  json += "\"power\":" + String(displayOn ? "true" : "false");
  json += "}";
  
  server.send(200, "application/json", json);
}

void handlePower() {
  if (server.hasArg("state")) {
    displayOn = (server.arg("state") == "1");
    
    if (!displayOn) {
      // Clear display when turning off
      writeToRegisters(0xFF, 0xFFFF);
    }
    
    Serial.print("Display power: ");
    Serial.println(displayOn ? "ON" : "OFF");
    
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing state parameter");
  }
}
