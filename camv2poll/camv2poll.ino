/*
   sep26
   added flash when connecting to wifi
   added bucket + table
   added watch Poll
   includes #include <ArduinoJson.h>
   added filter cp_status(for approval, live request)
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h" //disable brownout problems
#include "soc/rtc_cntl_reg.h"  //disable brownout problems
#include "esp_http_server.h"
#include <ArduinoJson.h>

// Replace with your network credentials
const char* ssid = "ken tas violet";
const char* password = "@Kenvelbis2022";

// Supabase Storage bucket and API info
const char* supabase_url = "https://uiciowpyxfawjvaddivu.supabase.co";
const char* supabase_key = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVpY2lvd3B5eGZhd2p2YWRkaXZ1Iiwicm9sZSI6InNlcnZpY2Vfcm9sZSIsImlhdCI6MTcyNzE0MDQyMCwiZXhwIjoyMDQyNzE2NDIwfQ.kR8PsVyqtW0QTJoFjFq6aiXU-iq0y3alXfJQIRMVgBw";

const char* storage_bucket = "images/public";

// Table endpoint for controls and images
const char* controls_table_url = "https://uiciowpyxfawjvaddivu.supabase.co/rest/v1/controls";
const char* images_table_url = "https://uiciowpyxfawjvaddivu.supabase.co/rest/v1/images";

// ESP32-CAM pin definition for AI Thinker module
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
#define FLASH_GPIO_NUM     4 // Pin for the flash LED

#define LEDC_CHANNEL      0  // PWM channel for flash LED
#define LEDC_TIMER        0  // PWM timer
#define LEDC_BASE_FREQ    5000 // Base frequency for the PWM signal

unsigned long lastPollTime = 0;  // For periodic polling
int pollInterval = 10000; // 10 seconds polling interval
int lastControlID = -1; // To keep track of the latest processed control

String imageName; // Declare imageName globally

// WiFi connection function with increased brightness
void connectToWiFi() {
  Serial.print("Connecting to WiFi");

  // Increase flash brightness during WiFi connection
  ledcWrite(LEDC_CHANNEL, 100); // Max brightness (0-255)

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("Connected to WiFi!");

  // Turn off the flash after connection
  ledcWrite(LEDC_CHANNEL, 0); // Turn off flash
}

// Skip initial frames for better image quality
camera_fb_t* skipFrames(int skip_count) {
  camera_fb_t* fb = NULL;
  for (int i = 0; i < skip_count; i++) {
    fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);  // Return the frame buffer without using it
    }
  }
  // Capture the final frame to upload
  fb = esp_camera_fb_get();
  return fb;
}

// Function to insert image details into the 'images' table
void addImageRecordToTable(String imageName, String payload) {
  HTTPClient http;
  String imageUrl = String(supabase_url) + "/storage/v1/object/" + storage_bucket + "/" + imageName + ".png";

  http.begin(images_table_url); // Endpoint to 'images' table
  http.addHeader("Authorization", String("Bearer ") + supabase_key);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabase_key);

  // Use the provided dynamic payload
  int httpResponseCode = http.POST(payload);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Record added successfully to 'images' table! Response: " + response);
  } else {
    Serial.println("Error adding record to table. HTTP response code: " + String(httpResponseCode));
  }

  // End HTTP connection
  http.end();
}

// Capture and upload image to Supabase with flash
void captureAndUploadImage(String payloadTemplate) {
  // Turn on the flash before capturing the image
  ledcWrite(LEDC_CHANNEL, 100); // Max brightness for flash

  // Skip initial frames to get the best quality image
  camera_fb_t *fb = skipFrames(4);  // Skip 4 frames

  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  // Create unique image name
  imageName = "upload-" + String(millis()) + ".png";  // Add .png here for consistency

  HTTPClient http;
  String url = String(supabase_url) + "/storage/v1/object/" + storage_bucket + "/" + imageName;
  http.begin(url.c_str());
  http.addHeader("Authorization", String("Bearer ") + supabase_key);
  http.addHeader("Content-Type", "image/png");

  // Send the captured image to Supabase
  int httpResponseCode = http.POST(fb->buf, fb->len);
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Image uploaded successfully! Response: " + response);


    String imageUrl = String(supabase_url) + "/storage/v1/object/public/" + storage_bucket + "/" + imageName;
    String payload = payloadTemplate;
    payload.replace("{image_url}", imageUrl);
    addImageRecordToTable(imageName, payload);
  } else {
    Serial.println("Error uploading image. HTTP response code: " + String(httpResponseCode));
  }

  // Turn off the flash after image capture
  ledcWrite(LEDC_CHANNEL, 0); // Turn off flash

  // Close the connection and free the frame buffer
  http.end();
  esp_camera_fb_return(fb);
}

void pollControlsTable() {
  HTTPClient http;
  String url = String(controls_table_url) + "?order=id.desc";
  http.begin(url);  // Supabase REST API URL for 'controls' table
  http.addHeader("Authorization", String("Bearer ") + supabase_key);
  http.addHeader("apikey", supabase_key);

  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Controls response: " + response);


    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);


    if (doc.size() > 0) {
      int controlID = doc[0]["id"];  // Get the id of the most recent control
      String who = doc[0]["who"];    // Get the "who" field

      if (controlID != lastControlID) {
        lastControlID = controlID;  // Update last processed control ID
        Serial.println("New control found, capturing image...");

        // Determine payload based on "who" field
        String payloadTemplate;

        if (who == "live request CP1") {
          payloadTemplate = "{\"image_url\": \"{image_url}\", \"status\": \"LIVE\", \"cp_id\": \"CP1\"}";
        } else if (who == "for approval") {
          payloadTemplate = "{\"image_url\": \"{image_url}\", \"status\": \"for approval\", \"cp_id\": \"CP1\"}";
        } else {
          Serial.println("Unknown 'who' field value: " + who);
          return;
        }


        captureAndUploadImage(payloadTemplate);
      } else {
        Serial.println("No new control.");
      }
    } else {
      Serial.println("No controls found.");
    }
  } else {
    Serial.println("Error in polling controls table. HTTP response code: " + String(httpResponseCode));
  }

  // End HTTP connection
  http.end();
}


// Retrieve latest control ID on startup
void retrieveLatestControlID() {
  HTTPClient http;
  String url = String(controls_table_url) + "?order=id.desc&limit=1";
  http.begin(url);  // Supabase REST API URL for 'controls' table
  http.addHeader("Authorization", String("Bearer ") + supabase_key);
  http.addHeader("apikey", supabase_key);

  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Latest control response: " + response);


    DynamicJsonDocument doc(512);
    deserializeJson(doc, response);

    if (doc.size() > 0) {
      lastControlID = doc[0]["id"];
      Serial.println("Latest control ID: " + String(lastControlID));
    }
  } else {
    Serial.println("Error retrieving latest control ID. HTTP response code: " + String(httpResponseCode));
  }

  // End the HTTP connection
  http.end();
}

void setup() {
  // Init Serial Monitor
  Serial.begin(115200);

  // Configure the flash LED pin
  ledcSetup(LEDC_CHANNEL, LEDC_BASE_FREQ, 8);
  ledcAttachPin(FLASH_GPIO_NUM, LEDC_CHANNEL);

  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Initialize the camera
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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;


  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA; // 640x480
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA; // 320x240
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // Connect to WiFi
  connectToWiFi();


  retrieveLatestControlID();
}

void loop() {
  unsigned long currentTime = millis();


  if (currentTime - lastPollTime >= pollInterval) {
    pollControlsTable();
    lastPollTime = currentTime;
  }
}
