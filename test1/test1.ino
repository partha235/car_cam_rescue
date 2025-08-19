// this program is for toggle led with AP mode & static IP
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

// HTTP request buffer
String header;

// LED state
String ledState = "on";

// LED pin (GPIO 2, confirmed working)
const int ledPin = 4;

void setup() {
  Serial.begin(115200);
  
  // Initialize LED pin
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  Serial.println("LED pin initialized on GPIO 2");

  // Set up Access Point
  Serial.print("Setting AP (Access Point)...");
  WiFi.softAPConfig(local_IP, gateway, subnet); // Uncomment for static IP
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  
  server.begin();
  Serial.println("Server started");
}

void loop() {
  WiFiClient client = server.available();   // Listen for incoming clients

  if (client) {                             // New client connected
    Serial.println("New Client.");
    String currentLine = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
        header += c;
        if (c == '\n') {
          if (currentLine.length() == 0) {
            // Log full request
            Serial.println("Full request: " + header);

            // Handle LED requests
            if (header.indexOf("GET /led?state=on") >= 0) {
              Serial.println("LED ON command received");
              ledState = "on";
              digitalWrite(ledPin, HIGH);
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/plain");
              client.println("Connection: close");
              client.println();
              client.println("LED On");
            } else if (header.indexOf("GET /led?state=off") >= 0) {
              Serial.println("LED OFF command received");
              ledState = "off";
              digitalWrite(ledPin, LOW);
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/plain");
              client.println("Connection: close");
              client.println();
              client.println("LED Off");
            } else {
              // Serve HTML page
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println("Connection: close");
              client.println();
              
              client.println("<!DOCTYPE html><html>");
              client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
              client.println("<link rel=\"icon\" href=\"data:,\">");
              client.println("<style>");
              client.println("html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
              client.println(".button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;");
              client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
              client.println(".button2 { background-color: #555555;}");
              client.println("</style></head>");
              
              client.println("<body><h1>ESP32-CAM Insect Monitor</h1>");
              client.println("<p>LED (GPIO 4) - State: <span id=\"ledState\">" + ledState + "</span></p>");
              client.println("<p><button class=\"button\" onclick=\"toggleLed('on')\">ON</button>");
              client.println("<button class=\"button button2\" onclick=\"toggleLed('off')\">OFF</button></p>");
              client.println("<p id=\"status\">Status: Waiting for action</p>");
              client.println("<script>");
              client.println("function toggleLed(state) {");
              client.println("  document.getElementById('status').textContent = 'Status: Sending ' + state;");
              client.println("  fetch('/led?state=' + state)");
              client.println("    .then(response => response.text())");
              client.println("    .then(data => {");
              client.println("      console.log(data);");
              client.println("      document.getElementById('ledState').textContent = state;");
              client.println("      document.getElementById('status').textContent = 'Status: ' + data;");
              client.println("    })");
              client.println("    .catch(error => {");
              client.println("      console.error('Error:', error);");
              client.println("      document.getElementById('status').textContent = 'Status: Error';");
              client.println("    });");
              client.println("}");
              client.println("</script>");
              
              client.println("</body></html>");
              client.println();
            }
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    header = "";
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}