#include <Arduino.h>
#include <WiFi.h>

// Network credentials
const char* ssid = "ESP32-Access-Point";
const char* password = "123456789";

// Static IP configuration (comment out if not working)
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
      h1 { margin-top: 20px; color: #333; }
    </style>
  </head>
  <body class="noselect" align="center">
    <h1>Hello World</h1>
    <p>Welcome to the ESP32-CAM web server in AP mode.</p>
  </body>
</html>
)HTMLHOMEPAGE";

void setup() {
  Serial.begin(115200);
  delay(1000); // Stabilize Serial
  Serial.println("Setup started");

  // Set up Access Point
  Serial.println("Setting AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_IP, gateway, subnet); 
  Serial.println("WiFi mode set to AP");
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

  // Start server
  Serial.println("Starting server...");
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  WiFiClient client = server.available(); // Listen for clients
  if (client) {
    Serial.println("New client connected");
    String currentLine = "";
    bool sentResponse = false;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.print(c); // Log each character for debugging
        currentLine += c;
        if (c == '\n') {
          Serial.println("Line received: " + currentLine);
          if (currentLine.startsWith("GET / ") || currentLine.startsWith("GET / HTTP/1.")) {
            Serial.println("Handling root request");
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();
            client.println(htmlHomePage);
            sentResponse = true;
            break;
          } else if (currentLine.length() == 1 && sentResponse) { // Empty line after response
            break;
          } else if (currentLine.length() > 1) {
            Serial.println("Handling not found: " + currentLine);
            client.println("HTTP/1.1 404 Not Found");
            client.println("Content-type:text/plain");
            client.println("Connection: close");
            client.println();
            client.println("File Not Found");
            sentResponse = true;
            break;
          }
          currentLine = "";
        }
      }
    }
    client.stop();
    Serial.println("Client disconnected");
  }
}