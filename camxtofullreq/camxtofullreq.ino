/*
 * sep26
   added flash when connecting to wifi
   added bucket + table
   added watch Poll
   includes #include <ArduinoJson.h>
   added filter cp_status(for approval, live request)
   simulates button press "x" to report that the cp is full

*/


#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h" 
#include "soc/rtc_cntl_reg.h"  
#include "esp_http_server.h"
#include <ArduinoJson.h>

const char* ssid = "ken tas violet";
const char* password = "@Kenvelbis2022";

const char* supabase_url = "https://uiciowpyxfawjvaddivu.supabase.co";
const char* supabase_key = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVpY2lvd3B5eGZhd2p2YWRkaXZ1Iiwicm9sZSI6InNlcnZpY2Vfcm9sZSIsImlhdCI6MTcyNzE0MDQyMCwiZXhwIjoyMDQyNzE2NDIwfQ.kR8PsVyqtW0QTJoFjFq6aiXU-iq0y3alXfJQIRMVgBw";

const char* storage_bucket = "images/public";

const char* controls_table_url = "https://uiciowpyxfawjvaddivu.supabase.co/rest/v1/controls";
const char* images_table_url = "https://uiciowpyxfawjvaddivu.supabase.co/rest/v1/images";

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
#define FLASH_GPIO_NUM     4 

#define LEDC_CHANNEL      0  
#define LEDC_TIMER        0  
#define LEDC_BASE_FREQ    5000 

unsigned long lastPollTime = 0;  
int pollInterval = 10000; 
int lastControlID = -1; 

String imageName; 

void connectToWiFi() {
  Serial.print("Connecting to WiFi");

  ledcWrite(LEDC_CHANNEL, 100); 

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("Connected to WiFi!");

  ledcWrite(LEDC_CHANNEL, 0); 
}

camera_fb_t* skipFrames(int skip_count) {
  camera_fb_t* fb = NULL;
  for (int i = 0; i < skip_count; i++) {
    fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);  
    }
  }

  fb = esp_camera_fb_get();
  return fb;
}

void addImageRecordToTable(String imageName, String payload) {
  HTTPClient http;
  String imageUrl = String(supabase_url) + "/storage/v1/object/" + storage_bucket + "/" + imageName + ".png";

  http.begin(images_table_url); 
  http.addHeader("Authorization", String("Bearer ") + supabase_key);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabase_key);

  int httpResponseCode = http.POST(payload);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Record added successfully to 'images' table! Response: " + response);
  } else {
    Serial.println("Error adding record to table. HTTP response code: " + String(httpResponseCode));
  }

  http.end();
}

void captureAndUploadImage(String payloadTemplate) {

  ledcWrite(LEDC_CHANNEL, 100); 

  camera_fb_t *fb = skipFrames(7);  

  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  imageName = "upload-" + String(millis()) + ".png";  

  HTTPClient http;
  String url = String(supabase_url) + "/storage/v1/object/" + storage_bucket + "/" + imageName;
  http.begin(url.c_str());
  http.addHeader("Authorization", String("Bearer ") + supabase_key);
  http.addHeader("Content-Type", "image/png");

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

  ledcWrite(LEDC_CHANNEL, 0); 

  http.end();
  esp_camera_fb_return(fb);
}

void pollControlsTable() {
  HTTPClient http;
  String url = String(controls_table_url) + "?order=id.desc";  
  http.begin(url);  
  http.addHeader("Authorization", String("Bearer ") + supabase_key);
  http.addHeader("apikey", supabase_key);

  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String response = http.getString();

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);

    if (doc.size() > 0) {
      int controlID = doc[0]["id"];  
      String who = doc[0]["who"];    

      if (controlID != lastControlID) {
        lastControlID = controlID;  
        Serial.println("New control found, capturing image...");

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

  http.end();
}

void retrieveLatestControlID() {
  HTTPClient http;
  String url = String(controls_table_url) + "?order=id.desc&limit=1";  
  http.begin(url);  
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

  http.end();
}

void sendImageToCpApproval() {
  Serial.println("CP1 is full, sending image to cp_approval table");

  ledcWrite(LEDC_CHANNEL, 100); 

  camera_fb_t *fb = skipFrames(7);  

  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  String imageName = "cp_approval-" + String(millis()) + ".png";

  HTTPClient http;
  String url = String(supabase_url) + "/storage/v1/object/" + storage_bucket + "/" + imageName;
  http.begin(url.c_str());
  http.addHeader("Authorization", String("Bearer ") + supabase_key);
  http.addHeader("Content-Type", "image/png");

  int httpResponseCode = http.POST(fb->buf, fb->len);
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Image uploaded successfully! Response: " + response);

    String imageUrl = String(supabase_url) + "/storage/v1/object/public/" + storage_bucket + "/" + imageName;

    String payload = "{\"image_url\": \"" + imageUrl + "\", \"status\": \"full\", \"cp_name\": \"CP1\"}";

    http.begin(String(supabase_url) + "/rest/v1/cp_approval");
    http.addHeader("Authorization", String("Bearer ") + supabase_key);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", supabase_key);

    httpResponseCode = http.POST(payload);
    if (httpResponseCode > 0) {
      response = http.getString();
      Serial.println("Record added successfully to cp_approval table! Response: " + response);
    } else {
      Serial.println("Error adding record to cp_approval table. HTTP response code: " + String(httpResponseCode));
    }
  } else {
    Serial.println("Error uploading image. HTTP response code: " + String(httpResponseCode));
  }

  ledcWrite(LEDC_CHANNEL, 0); 

  http.end();
  esp_camera_fb_return(fb);
}
void setup() {

  Serial.begin(115200);

  ledcSetup(LEDC_CHANNEL, LEDC_BASE_FREQ, 8); 
  ledcAttachPin(FLASH_GPIO_NUM, LEDC_CHANNEL); 

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

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

    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {

    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  connectToWiFi();

  retrieveLatestControlID();
}

void loop() {
  unsigned long currentTime = millis();

  if (currentTime - lastPollTime >= pollInterval) {
    pollControlsTable();
    lastPollTime = currentTime;
  }

    if (Serial.available() > 0) {
    char incoming = Serial.read();
    if (incoming == 'x') {
      Serial.println("CP1 is full sending image to cpa_approval");
      sendImageToCpApproval();
    }
  }
}
