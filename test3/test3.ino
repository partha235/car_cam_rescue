#include <Arduino.h>
#include <WiFi.h>

// Declare LED pin
const int led = 4;

// Network credentials
const char* ssid = "ESP32-Access-Point";
const char* password = "123456789";

// Static IP configuration
IPAddress local_IP(192, 168, 4, 23);
IPAddress gateway(192, 168, 4, 23);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

// Web server on port 80
WiFiServer server(80);

// HTML page
const char* htmlHomePage PROGMEM = R"HTMLHOMEPAGE(
<!DOCTYPE html>
<html>
  <head>
    <meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
    <style>
      body { font-family: Helvetica; text-align: center; background-color: white; margin: 0; }
      h1 { margin-top: 20px; }
      img { width: 80%; max-width: 480px; height: auto; }
      .button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;
                font-size: 24px; margin: 10px; cursor: pointer; }
      .button-off { background-color: #555555; }
      .status { font-size: 18px; color: #333; }
    </style>
  </head>
  <body class="noselect" align="center">
    <h2>LED Control (GPIO 4)</h2>
    <p>This program tests web page streaming with AP mode and static IP.</p>
    <p>LED State: <span id="ledState">off</span></p>
    <button class="button" onclick="toggleLed('on')">ON</button>
    <button class="button button-off" onclick="toggleLed('off')">OFF</button>
    <p id="status" class="status">Status: Waiting for action</p>
    <script>
      function toggleLed(state) {
        document.getElementById('status').textContent = 'Status: Sending ' + state;
        fetch('/led?state=' + state, { method: 'GET' })
          .then(response => response.text())
          .then(data => {
            console.log(data);
            document.getElementById('ledState').textContent = state;
            document.getElementById('status').textContent = 'Status: ' + data;
          })
          .catch(error => {
            console.error('Error:', error);
            document.getElementById('status').textContent = 'Status: Error';
          });
      }
    </script>
  </body>
</html>
)HTMLHOMEPAGE";

void setup() {
  Serial.begin(115200);
  delay(1000); // Stabilize Serial
  Serial.println("Setup started");

  // Set up Access Point with static IP
  Serial.println("Setting AP...");
  WiFi.mode(WIFI_AP);
  bool configSuccess = WiFi.softAPConfig(local_IP, gateway, subnet);
  if (configSuccess) {
    Serial.println("Static IP configuration successful");
  } else {
    Serial.println("Static IP configuration failed, using default");
  }
  bool apSuccess = WiFi.softAP(ssid, password);
  if (apSuccess) {
    Serial.println("AP started");
  } else {
    Serial.println("AP setup failed");
    return;
  }

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // Initialize LED pin
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);
  Serial.println("LED pin initialized on GPIO 4");

  // Start server
  Serial.println("Starting server...");
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  WiFiClient client = server.available(); // Listen for clients
  if (client) {
    Serial.println("New client connected");
    String requestBuffer = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.print(c); // Log each character for debugging
        requestBuffer += c;
        if (requestBuffer.endsWith("\r\n\r\n")) { // End of HTTP request
          Serial.println("\nFull request received: " + requestBuffer);
          if (requestBuffer.startsWith("GET / ")) {
            Serial.println("Handling root request");
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();
            client.println(htmlHomePage);
          } else if (requestBuffer.indexOf("GET /led?state=on") >= 0) {
            Serial.println("Handling LED ON request");
            digitalWrite(led, HIGH);
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/plain");
            client.println("Connection: close");
            client.println();
            client.println("LED On");
          } else if (requestBuffer.indexOf("GET /led?state=off") >= 0) {
            Serial.println("Handling LED OFF request");
            digitalWrite(led, LOW);
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/plain");
            client.println("Connection: close");
            client.println();
            client.println("LED Off");
          } else {
            Serial.println("Handling not found: " + requestBuffer);
            client.println("HTTP/1.1 404 Not Found");
            client.println("Content-type:text/plain");
            client.println("Connection: close");
            client.println();
            client.println("File Not Found");
          }
          break;
        }
      }
    }
    client.stop();
    Serial.println("Client disconnected");
  }
}