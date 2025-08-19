/* 
 * ESP32-CAM "Insect Monitor" Project
 * This code sets up the ESP32-CAM as an Access Point (AP) to stream a camera feed
 * and control an LED via a simple webpage. It uses synchronous WiFiServer for HTTP handling.
 */

#include <Arduino.h>           // Core Arduino functions for ESP32
#include <WiFi.h>              // WiFi library for network connectivity
#include <esp_camera.h>        // Camera library for ESP32-CAM
#include <esp_timer.h>         // High-resolution timer for precise timing
#include <img_converters.h>    // Image conversion utilities (included for potential future use)
#include <fb_gfx.h>            // Framebuffer graphics utilities (included for potential future use)
#include "soc/soc.h"           // Societal register definitions for brownout disable
#include "soc/rtc_cntl_reg.h"  // RTC control register for brownout disable

// Define the LED pin for control (GPIO 4 on AI-Thinker ESP32-CAM)
const int led = 4;

// Network credentials for the Access Point
const char* ssid = "ESP32-Access-Point";  // Name of the WiFi network
const char* password = "12345678";       // Password for the WiFi network (minimum 8 characters)

// Static IP configuration to ensure a consistent address
IPAddress local_IP(192, 168, 4, 23);      // Desired IP address for the AP
IPAddress gateway(192, 168, 4, 23);       // Gateway (same as IP for AP mode)
IPAddress subnet(255, 255, 255, 0);       // Subnet mask for the network
IPAddress primaryDNS(8, 8, 8, 8);         // Primary DNS (Google DNS)
IPAddress secondaryDNS(8, 8, 4, 4);       // Secondary DNS (Google DNS)

// Create a WiFiServer object to handle HTTP requests on port 80
WiFiServer server(80);

// Define a boundary string for MJPEG streaming (unique identifier for frame separation)
#define PART_BOUNDARY "123456789000000000000987654321"

// Define the camera model (uncomment only one based on your hardware)
#define CAMERA_MODEL_AI_THINKER
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WITHOUT_PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM_B
//#define CAMERA_MODEL_WROVER_KIT

// Camera pin configuration for AI-Thinker model
#if defined(CAMERA_MODEL_AI_THINKER)
  #define PWDN_GPIO_NUM     32  // Power down pin
  #define RESET_GPIO_NUM    -1  // Reset pin (not used, set to -1)
  #define XCLK_GPIO_NUM      0  // External clock pin
  #define SIOD_GPIO_NUM     26  // I2C data pin
  #define SIOC_GPIO_NUM     27  // I2C clock pin
  #define Y9_GPIO_NUM       35  // Y9 data pin
  #define Y8_GPIO_NUM       34  // Y8 data pin
  #define Y7_GPIO_NUM       39  // Y7 data pin
  #define Y6_GPIO_NUM       36  // Y6 data pin
  #define Y5_GPIO_NUM       21  // Y5 data pin
  #define Y4_GPIO_NUM       19  // Y4 data pin
  #define Y3_GPIO_NUM       18  // Y3 data pin
  #define Y2_GPIO_NUM        5  // Y2 data pin
  #define VSYNC_GPIO_NUM    25  // Vertical sync pin
  #define HREF_GPIO_NUM     23  // Horizontal reference pin
  #define PCLK_GPIO_NUM     22  // Pixel clock pin
#else
  #error "Camera model not selected"
#endif

// Constants for MJPEG streaming
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;  // MIME type for MJPEG
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";                         // Boundary separator
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";  // Header for each frame

// HTML content for the webpage
const char* htmlHomePage PROGMEM = R"HTMLHOMEPAGE(
<!DOCTYPE html>
<html>
    <head><title>Insect Monitor</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; text-align: center; margin-top: 30px; }
        button { font-size: 18px; padding: 10px 20px; margin: 10px; }
        img { width: 80%; max-width: 480px; height: auto; }
    </style>
    </head>
    <body>
        <h1>Insect Monitor</h1>
        <img src="/stream" alt="Live stream"><br>
        <h2>LED Control</h2>
        <button onclick="toggleLed('on')">ON</button>
        <button onclick="toggleLed('off')">OFF</button>
        <script>
            function toggleLed(state) {
                fetch('/led?state=' + state)
                    .then(response => response.text())
                    .then(data => console.log(data))
                    .catch(error => console.error('Error:', error));
            }
        </script>
    </body>
</html>
)HTMLHOMEPAGE";

/* 
 * Setup Function: Initializes hardware and network
 */
void setup() {
  // Disable brownout detector to prevent resets due to voltage dips
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Start serial communication for debugging
  Serial.begin(115200);
  delay(1000); // Give time for serial monitor to connect
  Serial.println("Setup started");

  // Configure camera settings
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;  // LED control channel
  config.ledc_timer = LEDC_TIMER_0;      // LED control timer
  config.pin_d0 = Y2_GPIO_NUM;           // Data pin 0
  config.pin_d1 = Y3_GPIO_NUM;           // Data pin 1
  config.pin_d2 = Y4_GPIO_NUM;           // Data pin 2
  config.pin_d3 = Y5_GPIO_NUM;           // Data pin 3
  config.pin_d4 = Y6_GPIO_NUM;           // Data pin 4
  config.pin_d5 = Y7_GPIO_NUM;           // Data pin 5
  config.pin_d6 = Y8_GPIO_NUM;           // Data pin 6
  config.pin_d7 = Y9_GPIO_NUM;           // Data pin 7
  config.pin_xclk = XCLK_GPIO_NUM;       // External clock
  config.pin_pclk = PCLK_GPIO_NUM;       // Pixel clock
  config.pin_vsync = VSYNC_GPIO_NUM;     // Vertical sync
  config.pin_href = HREF_GPIO_NUM;       // Horizontal reference
  config.pin_sscb_sda = SIOD_GPIO_NUM;   // I2C data
  config.pin_sscb_scl = SIOC_GPIO_NUM;   // I2C clock
  config.pin_pwdn = PWDN_GPIO_NUM;       // Power down
  config.pin_reset = RESET_GPIO_NUM;     // Reset (not used)
  config.xclk_freq_hz = 15000000;        // Reduced clock frequency for stability (15MHz)
  config.pixel_format = PIXFORMAT_JPEG;  // Output JPEG frames directly

  // Adjust resolution and buffer based on PSRAM availability
  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA;  // 320x240 resolution to reduce DMA load
    config.jpeg_quality = 12;            // Moderate quality for smaller file size
    config.fb_count = 1;                 // Single frame buffer to minimize memory use
  } else {
    config.frame_size = FRAMESIZE_QVGA;  // Same resolution without PSRAM
    config.jpeg_quality = 15;            // Slightly lower quality without PSRAM
    config.fb_count = 1;                 // Single buffer without PSRAM
  }

  // Initialize the camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);  // Log error if initialization fails
    return;
  }

  // Set up the Access Point with static IP
  Serial.println("Setting AP...");
  WiFi.mode(WIFI_AP);  // Set ESP32 to AP mode
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

  // Get and display the AP IP address
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // Initialize the LED pin
  pinMode(led, OUTPUT);         // Set LED pin as output
  digitalWrite(led, LOW);       // Turn LED off initially
  Serial.println("LED pin initialized on GPIO 4");

  // Start the web server
  Serial.println("Starting server...");
  server.begin();
  Serial.println("HTTP server started");
}

/* 
 * HandleClient Function: Processes incoming client requests
 * This function handles HTTP requests for the webpage, stream, and LED control.
 */
void handleClient(WiFiClient client) {
  String requestBuffer = "";  // Buffer to accumulate the HTTP request
  bool isStream = false;      // Flag to indicate stream request

  while (client.connected()) {  // Keep processing while client is connected
    if (client.available()) {   // Check if data is available from client
      char c = client.read();   // Read each character
      Serial.print(c);          // Log character for debugging
      requestBuffer += c;       // Add to request buffer

      // Check for end of HTTP request (double newline)
      if (requestBuffer.endsWith("\r\n\r\n")) {
        Serial.println("\nFull request received: " + requestBuffer);  // Log full request
        if (requestBuffer.startsWith("GET / ")) {                    // Handle root page
          Serial.println("Handling root request");
          client.println("HTTP/1.1 200 OK");                         // Send HTTP headers
          client.println("Content-Type: text/html");
          client.println("Connection: close");
          client.println();
          client.println(htmlHomePage);                              // Send HTML content
        } else if (requestBuffer.indexOf("GET /led?state=on") >= 0) { // Handle LED ON
          Serial.println("Handling LED ON request");
          digitalWrite(led, HIGH);                                   // Turn LED on
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/plain");
          client.println("Connection: close");
          client.println();
          client.println("LED On");
        } else if (requestBuffer.indexOf("GET /led?state=off") >= 0) { // Handle LED OFF
          Serial.println("Handling LED OFF request");
          digitalWrite(led, LOW);                                    // Turn LED off
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/plain");
          client.println("Connection: close");
          client.println();
          client.println("LED Off");
        } else if (requestBuffer.startsWith("GET /stream")) {         // Handle stream request
          Serial.println("Handling stream request");
          isStream = true;
          client.println("HTTP/1.1 200 OK");                         // Send MJPEG headers
          client.println("Content-Type: multipart/x-mixed-replace; boundary=" PART_BOUNDARY);
          client.println();
        } else {                                                    // Handle not found
          Serial.println("Handling not found: " + requestBuffer);
          client.println("HTTP/1.1 404 Not Found");
          client.println("Content-Type: text/plain");
          client.println("Connection: close");
          client.println();
          client.println("File Not Found");
        }
        if (!isStream) break;  // Exit for non-stream requests
      }

      // Handle streaming if flag is set
      if (isStream) {
        camera_fb_t *fb = esp_camera_fb_get();  // Get a frame from the camera
        if (fb) {
          if (fb->format == PIXFORMAT_JPEG) {   // Ensure frame is JPEG
            char part_buf[64];                  // Buffer for header
            size_t hlen = snprintf(part_buf, 64, _STREAM_PART, fb->len);  // Create header
            client.write(part_buf, hlen);       // Send header
            client.write((const char *)fb->buf, fb->len);  // Send image data
            client.write(_STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));  // Send boundary
          } else {
            Serial.println("Unexpected frame format");  // Log error if not JPEG
          }
          esp_camera_fb_return(fb);  // Release the frame buffer
          delay(200);                // Control frame rate to 5 FPS, reducing DMA load
        }
      }
    }
  }
  client.stop();  // Close client connection
  Serial.println("Client disconnected");
}

/* 
 * Loop Function: Continuously checks for new clients
 */
void loop() {
  WiFiClient client = server.available();  // Check for incoming clients
  if (client) {
    Serial.println("New client connected");  // Log new connection
    handleClient(client);                   // Process the client request
  }
}