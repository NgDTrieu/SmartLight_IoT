#include <Arduino.h>
#include <RBDdimmer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ================= CẤU HÌNH NGƯỜI DÙNG (SỬA Ở ĐÂY) =================
const char* ssid = "P302";        // <-- Điền tên Wifi
const char* password = "999999999";       // <-- Điền pass Wifi
const char* mqtt_server = "192.168.32.101";      // <-- Điền IP máy tính chạy Broker (VD: 192.168.1.10)
const int mqtt_port = 1883;

// ================= CẤU HÌNH HỆ THỐNG (KHÔNG CẦN SỬA) =================
const char* topic_status = "iot/light/status";   // Topic gửi dữ liệu đi
const char* topic_command = "iot/light/command"; // Topic nhận lệnh về

#define LDR_PIN     34
#define OUTPUT_PIN  14
#define ZC_PIN      27

// Khởi tạo các đối tượng
dimmerLamp dimmer(OUTPUT_PIN, ZC_PIN);
WiFiClient espClient;
PubSubClient client(espClient);

// Biến toàn cục (Giữ nguyên logic cũ)
bool isAutoMode = true;
int currentBrightness = 0;
float filteredValue = 0;
unsigned long lastMsg = 0;
char msgBuffer[256]; // Buffer tĩnh để chống tràn RAM

// ================= HÀM XỬ LÝ LỆNH JSON =================
// Hàm này thay thế cho việc đọc Serial trước đây
void processJsonCommand(String jsonString) {
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error) {
    Serial.print("JSON Error: ");
    Serial.println(error.c_str());
    return;
  }

  // Kiểm tra xem lệnh có hợp lệ không
  if (!doc.containsKey("type")) return;
  const char* type = doc["type"]; 

  // --- XỬ LÝ LỆNH MANUAL ---
  if (strcmp(type, "MANUAL") == 0) {
      isAutoMode = false;
      
      if (doc.containsKey("state") && strcmp(doc["state"], "OFF") == 0) {
          dimmer.setPower(0);
          currentBrightness = 0;
      } else {
          int brightness = doc["brightness"];
          // Giới hạn phần cứng (Safety)
          if (brightness > 70) brightness = 70;
          if (brightness < 0) brightness = 0;
          
          dimmer.setPower(brightness);
          currentBrightness = brightness;
      }
      // Gửi xác nhận (ACK) ngược lại Broker
      client.publish(topic_status, "{\"msg\": \"ACK_MANUAL\"}");
  }
  // --- XỬ LÝ LỆNH AUTO ---
  else if (strcmp(type, "AUTO") == 0) {
      bool enable = doc["enable"];
      isAutoMode = enable;
      client.publish(topic_status, "{\"msg\": \"ACK_AUTO\"}");
  }
}

// ================= CALLBACK MQTT =================
// Hàm này tự động chạy khi có tin nhắn từ Broker gửi xuống
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");

  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // Gọi hàm xử lý logic
  processJsonCommand(message);
}

// ================= KẾT NỐI LẠI =================
void reconnect() {
  // Lặp lại cho đến khi kết nối được
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    // Tạo ID ngẫu nhiên để không bị đá kết nối
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Sau khi kết nối thành công, phải ĐĂNG KÝ (Subscribe) topic nhận lệnh ngay
      client.subscribe(topic_command);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  
  // 1. Cấu hình Dimmer
  dimmer.begin(NORMAL_MODE, ON);
  dimmer.setPower(0);
  
  // 2. Cấu hình Cảm biến
  pinMode(LDR_PIN, INPUT);
  filteredValue = analogRead(LDR_PIN);

  // 3. Kết nối WiFi
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP()); // In IP để debug nếu cần

  // 4. Cấu hình MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback); // Đăng ký hàm xử lý khi có tin nhắn
}

// ================= LOOP =================
void loop() {
  // --- 1. DUY TRÌ KẾT NỐI MQTT ---
  if (!client.connected()) {
    reconnect();
  }
  client.loop(); // Hàm này cực quan trọng: để nhận tin nhắn và giữ kết nối

  // --- 2. ĐỌC & LỌC NHIỄU CẢM BIẾN ---
  int rawValue = analogRead(LDR_PIN);
  filteredValue = (filteredValue * 0.95) + (rawValue * 0.05);
  int sensorSmooth = (int)filteredValue;

  // --- 3. LOGIC AUTO (Giữ nguyên từ code cũ) ---
  if (isAutoMode) {
      int nguong_SANG = 300;   
      int nguong_TOI  = 3500;
      int MIN_BRIGHTNESS = 10;
      int MAX_BRIGHTNESS = 70; // Giới hạn an toàn
      
      int targetPower = 0;

      if (sensorSmooth < nguong_SANG) {
          targetPower = 0;
      } else if (sensorSmooth > nguong_TOI) {
          targetPower = MAX_BRIGHTNESS;
      } else {
          targetPower = map(sensorSmooth, nguong_SANG, nguong_TOI, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
      }

      if (targetPower > MAX_BRIGHTNESS) targetPower = MAX_BRIGHTNESS;

      // Logic chống rung (Hysteresis)
      if (abs(targetPower - currentBrightness) >= 2) {
          dimmer.setPower(targetPower);
          currentBrightness = targetPower;
      }
  }

  // --- 4. GỬI TRẠNG THÁI LÊN BROKER ---
  unsigned long now = millis();
  if (now - lastMsg > 2000) { // Mỗi 2 giây gửi 1 lần
      lastMsg = now;
      
      // Dùng snprintf để đóng gói JSON an toàn
      snprintf(msgBuffer, sizeof(msgBuffer), 
        "{\"is_on\": %s, \"brightness\": %d, \"sensor_value\": %d, \"is_auto_mode\": %s, \"timestamp\": %lu}",
        (currentBrightness > 0) ? "true" : "false",
        currentBrightness,
        sensorSmooth,
        isAutoMode ? "true" : "false",
        now / 1000
      );
      
      // Bắn thẳng lên MQTT Broker (Thay vì in ra Serial như cũ)
      client.publish(topic_status, msgBuffer);
      
      // Vẫn in ra Serial để bạn debug nếu đang cắm dây vào máy tính
      Serial.println(msgBuffer); 
  }
  
  delay(50); // Delay nhỏ để giảm tải CPU
}