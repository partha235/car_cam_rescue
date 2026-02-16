#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ====== USER CONFIG ======
const char* ssid = "******";           // Your WiFi network SSID
const char* password = "*********";    // Your WiFi network password
const int ledPin = 4;                    // Onboard flash LED (GPIO 4)
const int defaultFPS = 5;                // Default streaming frame rate (FPS)

// ====== CAMERA PINS (AI-Thinker ESP32-CAM) ======
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#define LED_GPIO_NUM      4

// ====== MJPEG STREAMING CONSTANTS ======
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ====== INDEX PAGE ======
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-CAM Insect Monitor</title>
  <style>
    body { font-family: Arial; text-align: center; margin-top: 20px; }
    img { width: 320px; height: auto; }
    button { font-size: 16px; padding: 10px 20px; margin: 10px; }
  </style>
</head>
<body>
  <h1>ESP32-CAM Insect Monitor</h1>
  <img src="/stream" alt="Live Stream"><br>
  <button onclick="fetch('/control?state=on').then(() => console.log('LED On'));">LED ON</button>
  <button onclick="fetch('/control?state=off').then(() => console.log('LED Off'));">LED OFF</button>
  <br><a href="/capture">Capture Single Image</a>
</body>
</html>
)rawliteral";

// ====== CAMERA INITIALIZATION ======
bool initCamera() {
  Serial.println("Starting camera initialization...");
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

 // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
    #if CONFIG_IDF_TARGET_ESP32S3
        config.fb_count = 2;
    #endif
      }
    
    #if defined(CAMERA_MODEL_ESP_EYE)
      pinMode(13, INPUT_PULLUP);
      pinMode(14, INPUT_PULLUP);
    #endif
    
      // camera init
      esp_err_t err = esp_camera_init(&config);
      if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x", err);
        // return;
      }
    
      sensor_t *s = esp_camera_sensor_get();
      // initial sensors are flipped vertically and colors are a bit saturated
      if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1);        // flip it back
        s->set_brightness(s, 1);   // up the brightness just a bit
        s->set_saturation(s, -2);  // lower the saturation
      }
      // drop down frame size for higher initial frame rate
      if (config.pixel_format == PIXFORMAT_JPEG) {
        s->set_framesize(s, FRAMESIZE_QVGA);
      }
    
    #if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
      s->set_vflip(s, 1);
      s->set_hmirror(s, 1);
    #endif
    #if defined(CAMERA_MODEL_ESP32S3_EYE)
      s->set_vflip(s, 1);
    #endif
        
    // Setup LED FLash if LED pin is defined in camera_pins.h
    #if defined(LED_GPIO_NUM)
      setupLedFlash();
    #endif
}

// ====== LED INITIALIZATION ======
void setupLedFlash() {
  Serial.println("Initializing LED...");
  pinMode(LED_GPIO_NUM, OUTPUT);
  digitalWrite(LED_GPIO_NUM, LOW);
  Serial.println("LED initialized on GPIO 4");
}

// ====== HTTP HANDLERS ======
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
  Serial.println("Served index page");
  return ESP_OK;
}

static esp_err_t control_handler(httpd_req_t *req) {
  char query[32];
  char value[16];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    if (httpd_query_key_value(query, "state", value, sizeof(value)) == ESP_OK) {
      if (strcmp(value, "on") == 0) {
        digitalWrite(LED_GPIO_NUM, HIGH);
        httpd_resp_sendstr(req, "LED On");
        Serial.println("LED turned ON");
        return ESP_OK;
      } else if (strcmp(value, "off") == 0) {
        digitalWrite(LED_GPIO_NUM, LOW);
        httpd_resp_sendstr(req, "LED Off");
        Serial.println("LED turned OFF");
        return ESP_OK;
      }
    }
  }
  httpd_resp_set_status(req, "400 Bad Request");
  httpd_resp_sendstr(req, "Invalid state");
  Serial.println("Invalid LED control request");
  return ESP_FAIL;
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Failed to capture single frame");
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "Failed to capture image");
    return ESP_FAIL;
  }
  if (fb->format != PIXFORMAT_JPEG) {
    Serial.println("Non-JPEG frame format in capture");
    esp_camera_fb_return(fb);
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "Invalid image format");
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  Serial.println("Served single image capture");
  return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  char part_buf[64];
  int fps = defaultFPS;
  unsigned long lastFrameTime = millis();
  const unsigned long streamTimeout = 30000; // 30s timeout for stream inactivity

  // Parse FPS from query string, if provided
  char query[32];
  char value[16];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    if (httpd_query_key_value(query, "fps", value, sizeof(value)) == ESP_OK) {
      fps = atoi(value);
      if (fps < 1 || fps > 30) fps = defaultFPS; // Limit FPS to 1-30
    }
  }
  Serial.printf("Starting stream at %d FPS\n", fps);

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    Serial.println("Failed to set stream content type");
    return res;
  }

  // Check sensor status
  sensor_t *s = esp_camera_sensor_get();
  if (!s) {
    Serial.println("Failed to get camera sensor");
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "Camera sensor unavailable");
    return ESP_FAIL;
  }

  int retryCount = 0;
  const int maxRetries = 3; // Retry camera capture up to 3 times
  while (true) {
    if (millis() - lastFrameTime > streamTimeout) {
      Serial.println("Stream timed out");
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_sendstr(req, "Stream timed out");
      res = ESP_FAIL;
      break;
    }

    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Failed to capture frame");
      retryCount++;
      if (retryCount >= maxRetries) {
        Serial.println("Max retries reached, giving up");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Failed to capture frame");
        res = ESP_FAIL;
        break;
      }
      // Attempt to reinitialize camera
      Serial.println("Reinitializing camera...");
      esp_camera_deinit();
      if (!initCamera()) {
        Serial.println("Camera reinitialization failed");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "Camera reinitialization failed");
        res = ESP_FAIL;
        break;
      }
      continue;
    }
    retryCount = 0; // Reset retry count on successful capture
    lastFrameTime = millis(); // Update last frame time

    if (fb->format != PIXFORMAT_JPEG) {
      Serial.println("Non-JPEG frame format");
      esp_camera_fb_return(fb);
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_sendstr(req, "Invalid frame format");
      res = ESP_FAIL;
      break;
    }

    Serial.printf("Captured frame, size: %u bytes\n", fb->len);
    size_t hlen = snprintf(part_buf, sizeof(part_buf), _STREAM_PART, fb->len);
    res = httpd_resp_send_chunk(req, part_buf, hlen);
    if (res != ESP_OK) {
      esp_camera_fb_return(fb);
      Serial.println("Failed to send stream header");
      break;
    }
    res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    if (res != ESP_OK) {
      esp_camera_fb_return(fb);
      Serial.println("Failed to send frame data");
      break;
    }
    res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    if (res != ESP_OK) {
      esp_camera_fb_return(fb);
      Serial.println("Failed to send stream boundary");
      break;
    }
    esp_camera_fb_return(fb);
    Serial.println("Frame sent successfully");

    delay(1000 / fps); // Adjust delay for desired FPS
  }

  return res;
}

// ====== CAMERA SERVER ======
httpd_handle_t camera_httpd = NULL;

void startCameraServer() {
  Serial.println("Starting web server...");
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 8; // Adjusted for added capture handler

  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
  };

  httpd_uri_t control_uri = {
    .uri = "/control",
    .method = HTTP_GET,
    .handler = control_handler,
    .user_ctx = NULL
  };

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
  };

  httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL
  };

  Serial.printf("Starting web server on port: '%d'\n", config.server_port);
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &control_uri);
    httpd_register_uri_handler(camera_httpd, &stream_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    Serial.println("Web server started successfully");
  } else {
    Serial.println("Failed to start web server");
  }
}

// ====== SETUP ======
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println("Starting setup...");
  delay(500); // Wait for serial monitor

  // Initialize LED
  setupLedFlash();

  // Initialize camera
  if (!initCamera()) {
    Serial.println("Camera init failed, entering error state");
    while (true) {
      digitalWrite(LED_GPIO_NUM, HIGH);
      delay(500);
      digitalWrite(LED_GPIO_NUM, LOW);
      delay(500); // Blink LED to indicate failure
    }
  }

  // Connect to WiFi in Station mode
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for connection with timeout
  unsigned long startAttemptTime = millis();
  const unsigned long timeout = 30000; // 30 seconds timeout
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < timeout) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed");
    while (true) {
      digitalWrite(LED_GPIO_NUM, HIGH);
      delay(250);
      digitalWrite(LED_GPIO_NUM, LOW);
      delay(250); // Fast blink for WiFi failure
    }
  }

  Serial.println("");
  Serial.print("Connected to WiFi, IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("WiFi Signal Strength (RSSI): ");
  Serial.println(WiFi.RSSI());

  // Start server
  startCameraServer();
  Serial.println("Camera Ready! Use 'http://" + WiFi.localIP().toString() + "' to connect");
}

// ====== LOOP ======
void loop() {
  // Monitor WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, attempting to reconnect...");
    WiFi.reconnect();
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Reconnected to WiFi, IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("Reconnection failed");
    }
  }
  delay(10000); // Check every 10 seconds
}