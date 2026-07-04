#include <esp_now.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "FS.h"
#include "SD_MMC.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// مكتبات الذكاء الاصطناعي
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#include "face_recognition_112_v1_s8.hpp" 

// ================= إعدادات الاتصال =================
uint8_t esp32Mac[] = {0x80, 0xF3, 0xDA, 0x5D, 0x6F, 0x7C}; // MAC البوردة الأساسية
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ================= إعدادات تليجرام =================
String BOT_TOKEN = "YOUR_BOT_TOKEN";
String CHAT_ID = "YOUR_CHAT_ID";

typedef struct { 
  char cmd[16];
} Packet;
Packet rxPacket;
Packet txPacket;

volatile bool motionFlag = false;

// ---- إعدادات الكاميرا ----
#define PWDN_GPIO_NUM    32
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    0
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM      18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM    23
#define PCLK_GPIO_NUM    22
#define FLASH_PIN        4

int picNumber = 0;
bool isFaceEnrolled = false; 

HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);
FaceRecognition112V1S8 recognizer; 

// ---------------------------------------------------------
void initCamera() {
  camera_config_t config;
  config.ledc_channel  = LEDC_CHANNEL_0;
  config.ledc_timer    = LEDC_TIMER_0;
  config.pin_d0        = Y2_GPIO_NUM;
  config.pin_d1        = Y3_GPIO_NUM;
  config.pin_d2        = Y4_GPIO_NUM;
  config.pin_d3        = Y5_GPIO_NUM;
  config.pin_d4        = Y6_GPIO_NUM;
  config.pin_d5        = Y7_GPIO_NUM;
  config.pin_d6        = Y8_GPIO_NUM;
  config.pin_d7        = Y9_GPIO_NUM;
  config.pin_xclk      = XCLK_GPIO_NUM;
  config.pin_pclk      = PCLK_GPIO_NUM;
  config.pin_vsync     = VSYNC_GPIO_NUM;
  config.pin_href      = HREF_GPIO_NUM;
  config.pin_sscb_sda  = SIOD_GPIO_NUM;
  config.pin_sscb_scl  = SIOC_GPIO_NUM;
  config.pin_pwdn      = PWDN_GPIO_NUM;
  config.pin_reset     = RESET_GPIO_NUM;
  config.xclk_freq_hz  = 20000000;
  config.pixel_format  = PIXFORMAT_RGB565; 

  if (psramFound()) {
    config.frame_size    = FRAMESIZE_QVGA; 
    config.jpeg_quality  = 10;
    config.fb_count      = 2;
  } else {
    config.frame_size    = FRAMESIZE_QVGA;
    config.jpeg_quality  = 12;
    config.fb_count      = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    ESP.restart();
  }

  sensor_t* s = esp_camera_sensor_get();
  s->set_gain_ctrl(s, 1);
  s->set_exposure_ctrl(s, 1);
  s->set_awb_gain(s, 1);
  s->set_brightness(s, 1);
  s->set_contrast(s, 1);   
  s->set_saturation(s, 1); 
  s->set_aec_value(s, 300);
  s->set_vflip(s, 1);    
  s->set_hmirror(s, 1);  
}

// ---------------------------------------------------------
void sendOpenDoorSignal() {
  strcpy(txPacket.cmd, "OPEN_DOOR");
  esp_now_send(esp32Mac, (uint8_t *) &txPacket, sizeof(txPacket));
  Serial.println("Signal 'OPEN_DOOR' sent");
}

void sendUnknownSignal() {
  strcpy(txPacket.cmd, "UNKNOWN");
  esp_now_send(esp32Mac, (uint8_t *) &txPacket, sizeof(txPacket));
  Serial.println("Signal 'UNKNOWN' sent");
}

// ---------------------------------------------------------
// دالة إرسال الصورة على تليجرام
// ---------------------------------------------------------
// دالة إرسال الصورة على تليجرام (معدلة للـ Debugging)
void sendPhotoTelegram(uint8_t* imageBuf, size_t imageLen) {
  WiFiClientSecure client;
  client.setInsecure(); // تخطي فحص شهادة الـ SSL

  Serial.println("Connecting to Telegram Server...");
  if (client.connect("api.telegram.org", 443)) {
    Serial.println("Connected! Uploading photo...");
    
    String head = "--MyBoundary\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + CHAT_ID + "\r\n";
    head += "--MyBoundary\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"intruder.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--MyBoundary--\r\n";

    size_t totalLen = head.length() + imageLen + tail.length();

    client.println("POST /bot" + BOT_TOKEN + "/sendPhoto HTTP/1.1");
    client.println("Host: api.telegram.org");
    client.println("Connection: close"); // أمر مهم جداً عشان السيرفر ميقفلش في وشنا
    client.println("Content-Length: " + String(totalLen));
    client.println("Content-Type: multipart/form-data; boundary=MyBoundary");
    client.println();
    client.print(head);

    // رفع الصورة
    size_t chunkSize = 1024;
    for (size_t i = 0; i < imageLen; i += chunkSize) {
      size_t len = min(chunkSize, imageLen - i);
      client.write(imageBuf + i, len);
    }
    client.print(tail);

    Serial.println("Photo sent! Waiting for Telegram response...");
    
    // قراءة الرد من سيرفر تليجرام وطباعته
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      Serial.println(line); 
    }
    client.stop();
    Serial.println("Connection closed.");
  } else {
    Serial.println("Telegram connection failed! Check WiFi or DNS.");
  }
}
// ---------------------------------------------------------
// ---------------------------------------------------------
void captureAndSave() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return;
  esp_camera_fb_return(fb);
  delay(100);

  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Capture failed");
    return;
  }

  bool intruderAlert = false; // علم أمني لمعرفة هنبعت لتليجرام ولا لأ

  std::list<dl::detect::result_t>& candidates = s1.infer((uint16_t*)fb->buf, {(int)fb->height, (int)fb->width, 3});
  std::list<dl::detect::result_t>& results = s2.infer((uint16_t*)fb->buf, {(int)fb->height, (int)fb->width, 3}, candidates);
    
  if (results.size() > 0) { // لو لقط وش فعلاً
    Serial.println(">>> FACE DETECTED! Proceeding to Recognition... <<<");
    
    if (!isFaceEnrolled) {
      Serial.println("Enrolling Face ID 1 (Owner)...");
      recognizer.enroll_id((uint16_t*)fb->buf, {(int)fb->height, (int)fb->width, 3}, results.front().keypoint, "", true);
      isFaceEnrolled = true;
      Serial.println("Enrolled Successfully!");
      sendOpenDoorSignal(); 
    } 
    else {
      auto rec_result = recognizer.recognize((uint16_t*)fb->buf, {(int)fb->height, (int)fb->width, 3}, results.front().keypoint);
      
      if (rec_result.id > 0) {
        // وش متطابق (صاحب البيت)
        Serial.println(">>> MATCH FOUND! Opening door... <<<");
        sendOpenDoorSignal();
      } else {
        // وش غريب 
        Serial.println(">>> STRANGER DETECTED! Denying access... <<<");
        sendUnknownSignal(); 
        intruderAlert = true; // تفعيل الإنذار لتليجرام عشان ده وش غريب
      }
    }
  } else {
    // مفيش وش (حركة وهمية أو شخص مغطي وشه بالكامل)
    Serial.println(">>> NO FACE DETECTED! Ignoring for Telegram... <<<");
    sendUnknownSignal(); // بنبعت للإساسية عشان السيستم ميعلقش
    intruderAlert = false; // مش هنبعت حاجة لتليجرام
  }
  
  // تحويل الصورة لـ JPEG
  size_t   jpgLen = 0;
  uint8_t* jpgBuf = NULL;
  bool converted = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_RGB565, 80, &jpgBuf, &jpgLen);
  esp_camera_fb_return(fb); 

  if (converted && jpgBuf) {
    // 1. الحفظ على الـ SD Card كـ Backup دايماً
    picNumber++;
    String path = "/pic" + String(picNumber) + ".jpg";
    File file = SD_MMC.open(path.c_str(), FILE_WRITE);
    if (file) {
      file.write(jpgBuf, jpgLen);
      file.close();
      Serial.printf("Saved to SD: %s\n", path.c_str());
    }

    // 2. الإرسال لتليجرام (لو كان وش غريب بس)
    if (intruderAlert) {
      sendPhotoTelegram(jpgBuf, jpgLen);
    }
    free(jpgBuf);
  }
}
// ---------------------------------------------------------
void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  memcpy(&rxPacket, data, sizeof(rxPacket));
  if (strcmp(rxPacket.cmd, "motion") == 0) {
    motionFlag = true;
  }
}

// ---------------------------------------------------------
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);
  delay(500);
  
  pinMode(FLASH_PIN, OUTPUT);
  digitalWrite(FLASH_PIN, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  initCamera();

  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("SD mount failed");
  } else {
    Serial.println("SD mounted OK");
  }

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    ESP.restart();
  }
  esp_now_register_recv_cb(onRecv);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, esp32Mac, 6);
  peer.channel = WiFi.channel();  
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  Serial.println("CAM Ready — waiting for motion signal...");
}

// ---------------------------------------------------------
void loop() {
  if (motionFlag) {
    motionFlag = false;
    captureAndSave();
  }
  delay(100);
}