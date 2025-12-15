#include <SoftwareSerial.h>

// A9G Module connections
SoftwareSerial a9g(10, 11); // RX, TX

void setup() {
  Serial.begin(115200);
  a9g.begin(115200);
  
  Serial.println("A9G Test Started");
  Serial.println("Setup Complete!");
  Serial.println("Board: A9G GSM/GPS Module");
  Serial.println("Baud Rate: 115200");
  
  // Test A9G communication
  delay(2000);
  a9g.println("AT");
  delay(1000);
}

void loop() {
  // Read from A9G and print to Serial
  if (a9g.available()) {
    String response = a9g.readString();
    Serial.print("A9G: ");
    Serial.println(response);
  }
  
  // Send commands from Serial to A9G
  if (Serial.available()) {
    String command = Serial.readString();
    a9g.print(command);
    Serial.print("Sent: ");
    Serial.println(command);
  }
  
  delay(100);
}
