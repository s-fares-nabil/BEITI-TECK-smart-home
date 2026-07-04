#define BLYNK_TEMPLATE_ID   "TMPL2orfgQ1Um"
#define BLYNK_TEMPLATE_NAME "Smart Home"
#define BLYNK_AUTH_TOKEN    "YOUR_BLYNK_AUTH_TOKEN"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <DHT.h>
#include <esp_now.h>
#include <ESP32Servo.h> 

// =====================================================================
uint8_t camMac[] = {0x8C, 0x4F, 0x00, 0xAB, 0xE2, 0x54};

// ---- Pin definitions ----
#define dhtpin      22
#define dhttype     DHT11
#define led         19
#define buzzerPin   18
#define fanPin      21
#define fireSensor  5
#define outerlight  33
#define pirPin      27  
#define gasPin      32
#define servoPin    13 

// ---- Outer light / PIR ----
int  blynkButtonState  = 0;
unsigned long lastMotionTime = 0;
bool motionActive      = false;
const unsigned long motionDelay = 60000L;
bool manualOverrideOff = false;
int  lastSyncState     = -1;

// ---- Gas / Temp / Fan ----
int   gasThreshold  = 2000;
int   blynkFanSpeed = 0;
float currentTemp   = 0.0;
int   isAutoMode    = 1;

// ---- ESP-NOW ----
typedef struct { char cmd[16]; } Packet;
Packet txPacket;
Packet rxPacket; 

DHT       dht(dhtpin, dhttype);
BlynkTimer timer;
Servo doorServo; 

// متغيرات التحكم في الباب بدون توقيف الكود (بديل الـ delay)
bool isDoorOpen = false;
bool lastDoorState = false;
unsigned long doorOpenTime = 0;
const unsigned long doorOpenDuration = 7000L; // 7 ثواني

char ssid[] = "YOUR_WIFI_SSID";
char pass[] = "YOUR_WIFI_PASSWORD";

// ---------------------------------------------------------
void onDataSent(const uint8_t* mac, esp_now_send_status_t status) {
  Serial.printf("ESP-NOW send: %s\n", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void onDataRecv(const uint8_t* mac, const uint8_t* data, int len) {
  memcpy(&rxPacket, data, sizeof(rxPacket));
  Serial.printf("ESP-NOW RX: %s\n", rxPacket.cmd);

  if (strcmp(rxPacket.cmd, "OPEN_DOOR") == 0) {
    Serial.println("Face Recognized! Opening door...");
    isDoorOpen = true;
    doorOpenTime = millis(); // تصفير عداد الوقت
  } 
  else if (strcmp(rxPacket.cmd, "UNKNOWN") == 0) {
    Serial.println("Unknown Face/No Face! Cycle reset.");
  }
}

// ---------------------------------------------------------
BLYNK_CONNECTED() {
  Blynk.syncVirtual(V5);  
  Blynk.syncVirtual(V6); // مزامنة حالة الباب عند الاتصال
}

// ---------------------------------------------------------
void tempsensor() {
  currentTemp   = dht.readTemperature();
  float hum     = dht.readHumidity();

  if (isnan(currentTemp) || isnan(hum)) {
    Serial.println("DHT read failed");
    return;
  }

  digitalWrite(led, currentTemp > 33.0 ? HIGH : LOW);

  if (isAutoMode == 1) {
    if      (currentTemp >= 33.0)                        analogWrite(fanPin, 204);
    else if (currentTemp >= 32.0 && currentTemp < 33.0)  analogWrite(fanPin, 153);
    else if (currentTemp >= 31.0 && currentTemp < 32.0)  analogWrite(fanPin, 51);
    else                                                 analogWrite(fanPin, 0);
  } else {
    analogWrite(fanPin, blynkFanSpeed);
  }

  Blynk.virtualWrite(V1, currentTemp);
}

// ---------------------------------------------------------
void handleLightLogic() {
  int currentMotion = digitalRead(pirPin);

  if (blynkButtonState == 0 && currentMotion == LOW) {
    manualOverrideOff = false;
  }

  if (currentMotion == HIGH && !manualOverrideOff && blynkButtonState == 0) {
    if (!motionActive) {
      strcpy(txPacket.cmd, "motion");
      for (int i = 0; i < 3; i++) {
        esp_now_send(camMac, (uint8_t*)&txPacket, sizeof(txPacket));
        delay(10);
      }
    }
    lastMotionTime = millis();
    motionActive   = true;
  }

  if (motionActive && (millis() - lastMotionTime >= motionDelay)) {
    motionActive = false;
  }

  int targetState = (blynkButtonState == 1 || motionActive) ? HIGH : LOW;
  digitalWrite(outerlight, targetState);

  if (targetState != lastSyncState) {
    Blynk.virtualWrite(V2, targetState);
    lastSyncState = targetState;
  }
}

// ---------------------------------------------------------
void handleSafetySensors() {
  int gasValue  = analogRead(gasPin);
  int fireState = digitalRead(fireSensor);

  Blynk.virtualWrite(V3, gasValue);

  if (gasValue > gasThreshold || fireState == LOW) {
    digitalWrite(buzzerPin, LOW); 
    if (fireState == LOW) Serial.println("DANGER: Fire Detected!");
    if (gasValue > gasThreshold)   Serial.println("DANGER: Gas Threshold Exceeded!");
  } else {
    digitalWrite(buzzerPin, HIGH);
  }
}

// ---------------------------------------------------------
BLYNK_WRITE(V2) {
  blynkButtonState = param.asInt();
  if (blynkButtonState == 0) {
    motionActive      = false;
    manualOverrideOff = true;
  }
  handleLightLogic();
}

BLYNK_WRITE(V4) {
  if (isAutoMode != 0) return; 

  int sliderValue = param.asInt();
  blynkFanSpeed   = map(sliderValue, 0, 100, 0, 255);
  analogWrite(fanPin, blynkFanSpeed);
}

BLYNK_WRITE(V5) {
  isAutoMode = param.asInt();
  if (isAutoMode == 0) {
    Serial.println("Fan mode: MANUAL");
    Blynk.setProperty(V4, "disabled", false);
    analogWrite(fanPin, blynkFanSpeed);
  } else {
    Serial.println("Fan mode: AUTO");
    Blynk.setProperty(V4, "disabled", true);
  }
}

// ==========================================
// V6 — تحكم السيرفو موتور (يدعم القفل المبكر)
// ==========================================
BLYNK_WRITE(V6) {
  if (param.asInt() == 1) {
    Serial.println("Blynk Command: Manual Door Open!");
    isDoorOpen = true;
    doorOpenTime = millis(); // تصفير العداد عشان يستنى 7 ثواني من دلوقتي
  } else {
    Serial.println("Blynk Command: Manual Door Close!");
    isDoorOpen = false; // اليوزر قرر يقفل الباب بدري
  }
}

// ---------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  dht.begin();
  pinMode(led,        OUTPUT);
  pinMode(outerlight, OUTPUT);
  pinMode(fanPin,     OUTPUT);
  pinMode(buzzerPin,  OUTPUT);
  pinMode(pirPin,     INPUT);
  pinMode(fireSensor, INPUT);
  pinMode(gasPin,     INPUT);

  digitalWrite(buzzerPin, HIGH); 

  doorServo.attach(servoPin);
  doorServo.write(0); 

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  Serial.print("Main ESP32 MAC  : ");
  Serial.println(WiFi.macAddress()); 
  Serial.print("WiFi Channel    : ");
  Serial.println(WiFi.channel());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    ESP.restart();
  }
  
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv); 

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, camMac, 6);
  peer.channel = WiFi.channel();  
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Failed to add ESP-NOW peer");
  }

  timer.setInterval(1000L, tempsensor);
  timer.setInterval(100L,  handleLightLogic);
  timer.setInterval(1000L, handleSafetySensors);

  Serial.println("Main ESP32 Ready");
}

// ---------------------------------------------------------
void loop() {
  Blynk.run();
  timer.run();

  // 1. فحص الوقت لغلق الباب التلقائي
  if (isDoorOpen) {
    if (millis() - doorOpenTime >= doorOpenDuration) {
      Serial.println("Time's up! Auto-closing door...");
      isDoorOpen = false;
    }
  }

  // 2. تحديث حركة السيرفو وحالة بلينك فقط عند حدوث تغيير
  if (isDoorOpen != lastDoorState) {
    if (isDoorOpen) {
      doorServo.write(180); // التعديل هنا: السيرفو هيلف 180 درجة
      Blynk.virtualWrite(V6, 1); 
    } else {
      doorServo.write(0);   // بيرجع للصفر يقفل
      Blynk.virtualWrite(V6, 0); 
    }
    lastDoorState = isDoorOpen;
  }
}