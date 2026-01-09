#include <Arduino.h>
#include <RBDdimmer.h>
#include <ArduinoJson.h>

// --- CẤU HÌNH CHÂN ---
#define LDR_PIN     34
#define OUTPUT_PIN  14
#define ZC_PIN      27

dimmerLamp dimmer(OUTPUT_PIN, ZC_PIN);

// --- BIẾN TOÀN CỤC ---
bool isAutoMode = true;
int currentBrightness = 0;
float filteredValue = 0;
unsigned long lastMsg = 0;

// Buffer cho JSON để không phải cấp phát động liên tục
char msgBuffer[256]; 

void setup() {
  Serial.begin(115200);
  
  dimmer.begin(NORMAL_MODE, ON);
  dimmer.setPower(0);
  
  pinMode(LDR_PIN, INPUT);
  filteredValue = analogRead(LDR_PIN);
  
  Serial.println("ESP32 SmartLight Optimized Started!");
}

void processCommand(String jsonString) {
  // Dùng StaticJsonDocument<200> để an toàn bộ nhớ (RAM cố định)
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error) {
    Serial.print("JSON Error: ");
    Serial.println(error.c_str());
    return;
  }

  // Kiểm tra an toàn trước khi đọc
  if (!doc.containsKey("type")) return;
  
  const char* type = doc["type"]; 

  if (strcmp(type, "MANUAL") == 0) {
      isAutoMode = false;
      
      if (doc.containsKey("state") && strcmp(doc["state"], "OFF") == 0) {
          dimmer.setPower(0);
          currentBrightness = 0;
      } else {
          int brightness = doc["brightness"];
          if (brightness > 70) brightness = 70;
          if (brightness < 0) brightness = 0;
          dimmer.setPower(brightness);
          currentBrightness = brightness;
      }
      Serial.println("{\"msg\": \"ACK_MANUAL\"}"); // Báo đã nhận lệnh
  }
  else if (strcmp(type, "AUTO") == 0) {
      bool enable = doc["enable"];
      isAutoMode = enable;
      Serial.println("{\"msg\": \"ACK_AUTO\"}"); // Báo đã nhận lệnh
  }
}

void loop() {
  // --- 1. NHẬN LỆNH ---
  if (Serial.available()) {
      // Đọc an toàn hơn, tránh treo nếu không có ký tự ngắt dòng
      String input = Serial.readStringUntil('\n');
      input.trim();
      if (input.length() > 0) processCommand(input);
  }

  // --- 2. ĐỌC CẢM BIẾN ---
  int rawValue = analogRead(LDR_PIN);
  filteredValue = (filteredValue * 0.95) + (rawValue * 0.05);
  int sensorSmooth = (int)filteredValue;

  // --- 3. LOGIC AUTO ---
  if (isAutoMode) {
      int nguong_SANG = 300;   
      int nguong_TOI  = 3500;
      int MIN_BRIGHTNESS = 10;
      int MAX_BRIGHTNESS = 70;
      
      int targetPower = 0;

      if (sensorSmooth < nguong_SANG) {
          targetPower = 0;
      } else if (sensorSmooth > nguong_TOI) {
          targetPower = MAX_BRIGHTNESS;
      } else {
          targetPower = map(sensorSmooth, nguong_SANG, nguong_TOI, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
      }

      if (targetPower > MAX_BRIGHTNESS) targetPower = MAX_BRIGHTNESS;

      // Logic chống rung
      if (abs(targetPower - currentBrightness) >= 2) {
          dimmer.setPower(targetPower);
          currentBrightness = targetPower;
      }
  }

  // --- 4. GỬI TRẠNG THÁI (Dùng snprintf thay vì cộng String) ---
  unsigned long now = millis();
  if (now - lastMsg > 2000) { 
      lastMsg = now;
      
      // snprintf giúp format chuỗi nhanh và an toàn
      snprintf(msgBuffer, sizeof(msgBuffer), 
        "{\"is_on\": %s, \"brightness\": %d, \"sensor_value\": %d, \"is_auto_mode\": %s, \"timestamp\": %lu}",
        (currentBrightness > 0) ? "true" : "false",
        currentBrightness,
        sensorSmooth,
        isAutoMode ? "true" : "false",
        now / 1000
      );
      
      Serial.println(msgBuffer);
  }
  
  delay(50);
}