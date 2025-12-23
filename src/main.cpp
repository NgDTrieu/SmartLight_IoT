#include <Arduino.h>
#include <RBDdimmer.h>
#include <ArduinoJson.h>

// --- CẤU HÌNH CHÂN (Sửa lại nếu bạn đấu dây khác) ---
#define LDR_PIN     34  // Chân cảm biến quang trở
#define OUTPUT_PIN  14  // Chân điều khiển Dimmer (PWM)
#define ZC_PIN      27  // Chân Zero-Cross

// --- KHỞI TẠO DIMMER ---
dimmerLamp dimmer(OUTPUT_PIN, ZC_PIN);

// --- BIẾN TOÀN CỤC ---
bool isAutoMode = true; // Mặc định bật lên là chạy tự động
int currentBrightness = 0;
float filteredValue = 0; // Biến lọc nhiễu cảm biến
unsigned long lastMsg = 0;

void setup() {
  Serial.begin(115200); // Tốc độ này phải khớp với Python Bridge
  
  dimmer.begin(NORMAL_MODE, ON); // Khởi động dimmer
  dimmer.setPower(0);
  
  pinMode(LDR_PIN, INPUT);
  filteredValue = analogRead(LDR_PIN); // Đọc giá trị đầu tiên
  
  Serial.println("ESP32 SmartLight Started!");
}

// --- HÀM XỬ LÝ LỆNH TỪ PYTHON GỬI XUỐNG ---
void processCommand(String jsonString) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error) return;

  const char* type = doc["type"]; 

  // --- TRƯỜNG HỢP: SERVER GỬI LỆNH MANUAL ---
  if (strcmp(type, "MANUAL") == 0) {
      isAutoMode = false; // Ngắt Auto ngay lập tức

      // Kiểm tra xem Server có bảo TẮT hẳn không?
      const char* state = doc["state"];
      if (state && strcmp(state, "OFF") == 0) {
          // NẾU LỆNH LÀ OFF -> ÉP VỀ 0 NGAY LẬP TỨC
          dimmer.setPower(0);
          currentBrightness = 0;
      } 
      else {
          // NẾU LỆNH LÀ ON (HOẶC CHỈNH ĐỘ SÁNG)
          int brightness = doc["brightness"];
          
          // Giới hạn phần cứng (như đã bàn ở bài trước)
          if (brightness > 70) brightness = 70;
          if (brightness < 0) brightness = 0;

          dimmer.setPower(brightness);
          currentBrightness = brightness;
      }
  }
  
  // --- TRƯỜNG HỢP: SERVER GỬI LỆNH AUTO ---
  else if (strcmp(type, "AUTO") == 0) {
      bool enable = doc["enable"];
      isAutoMode = enable;
      // Nếu tắt Auto từ đây thì giữ nguyên độ sáng hiện tại chờ lệnh mới
  }
}

void loop() {
  // --- 1. NHẬN LỆNH TỪ MÁY TÍNH (SERIAL) ---
  if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim(); 
      if (input.length() > 0) {
          processCommand(input);
      }
  }

  // --- 2. ĐỌC CẢM BIẾN & LỌC NHIỄU ---
  int rawValue = analogRead(LDR_PIN);
  // Công thức lọc: Lấy 95% giá trị cũ + 5% giá trị mới (giúp số nhảy mượt hơn)
  filteredValue = (filteredValue * 0.95) + (rawValue * 0.05);
  int sensorSmooth = (int)filteredValue;

  // --- 3. LOGIC TỰ ĐỘNG (CHỈ CHẠY KHI AUTO MODE = TRUE) ---
  if (isAutoMode) {
      // Dựa trên log thực tế: 
      // Sáng (Sensor thấp) -> Tắt đèn
      // Tối (Sensor cao) -> Bật đèn
      
      int nguong_SANG = 300;   
      int nguong_TOI  = 3500;  // Bạn nói sensor lên tầm hơn 3500
      
      // --- CẤU HÌNH GIỚI HẠN ĐỘ SÁNG ---
      int MIN_BRIGHTNESS = 10; // Đừng để thấp quá đèn sẽ nháy
      int MAX_BRIGHTNESS = 70; // <--- SỬA THÀNH 70 (Vì 90 bị tắt, 70 đã rất sáng)
      
      int targetPower = 0;

      if (sensorSmooth < nguong_SANG) {
          targetPower = 0; // Trời sáng -> Tắt hẳn
      } 
      else if (sensorSmooth > nguong_TOI) {
          targetPower = MAX_BRIGHTNESS; // Trời tối -> Sáng mức Max an toàn (70)
      } 
      else {
          // Map giá trị sensor vào khoảng an toàn (10 - 70)
          targetPower = map(sensorSmooth, nguong_SANG, nguong_TOI, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
      }

      // Giới hạn cứng thêm một lần nữa cho chắc
      if (targetPower > MAX_BRIGHTNESS) targetPower = MAX_BRIGHTNESS;

      // Chỉ chỉnh dimmer nếu giá trị thay đổi đáng kể (chống rung)
      if (abs(targetPower - currentBrightness) >= 2) {
          dimmer.setPower(targetPower);
          currentBrightness = targetPower;
      }
  }
  // --- 4. GỬI TRẠNG THÁI LÊN SERVER (MỖI 2 GIÂY) ---
  unsigned long now = millis();
  if (now - lastMsg > 2000) { 
      lastMsg = now;
      
      String isOn = (currentBrightness > 0) ? "true" : "false";
      String strAuto = isAutoMode ? "true" : "false";

      // Tạo JSON thủ công để gửi lên cho nhẹ
      String msg = "{";
      msg += "\"is_on\": " + isOn + ",";
      msg += "\"brightness\": " + String(currentBrightness) + ",";
      msg += "\"sensor_value\": " + String(sensorSmooth) + ",";
      msg += "\"is_auto_mode\": " + strAuto + ","; 
      msg += "\"timestamp\": " + String(millis()/1000); 
      msg += "}";
      
      Serial.println(msg); 
  }
  
  delay(50); // Delay nhỏ để MCU thở
}