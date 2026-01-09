import serial
import paho.mqtt.client as mqtt
import time
import sys
import threading

# --- CẤU HÌNH ---
SERIAL_PORT = 'COM3' 
BAUD_RATE = 115200

# Cấu hình MQTT Local
MQTT_BROKER = "localhost"
MQTT_PORT = 1883
MQTT_TOPIC_STATUS = "iot/light/status"   # ESP32 gửi lên
MQTT_TOPIC_COMMAND = "iot/light/command" # Server gửi xuống

# --- KHỞI TẠO KẾT NỐI SERIAL ---
try:
    print(f"🔌 Dang ket noi Serial {SERIAL_PORT}...")
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.05)
    
    # Thả chân DTR/RTS để không làm ESP32 bị Reset liên tục
    ser.dtr = False
    ser.rts = False
    
    time.sleep(1) 
    print(f"✅ Da mo cong {SERIAL_PORT} thanh cong!")
except Exception as e:
    print(f"❌ Khong the mo cong COM: {e}")
    print("👉 Goi y: Tat Serial Monitor trong VS Code hoac rut day cam lai.")
    sys.exit(1)

# --- XỬ LÝ MQTT ---
def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print(f"✅ Da ket noi MQTT Broker tai {MQTT_BROKER}:{MQTT_PORT}")
        client.subscribe(MQTT_TOPIC_COMMAND)
        print(f"🎧 Dang lang nghe lenh tu topic: {MQTT_TOPIC_COMMAND}")
    else:
        print(f"❌ Loi ket noi MQTT, ma loi: {rc}")

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode('utf-8')
        print(f"📥 Nhan lenh tu Server: {payload}")
        
        # Gửi xuống ESP32 qua Serial (Thêm \n để ESP32 biết hết câu)
        if ser.is_open:
            ser.write((payload + "\n").encode('utf-8'))
            print("   -> 🚀 Da ban xuong ESP32")
    except Exception as e:
        print(f"⚠️ Loi chuyen tiep lenh: {e}")

# Khởi tạo MQTT Client
client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
client.on_connect = on_connect
client.on_message = on_message

try:
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
    client.loop_start() # Chạy MQTT ở luồng riêng (background)
except Exception as e:
    print(f"❌ Khong the ket noi MQTT Broker: {e}")
    sys.exit(1)

# --- VÒNG LẶP CHÍNH: ĐỌC SERIAL -> GỬI MQTT ---
print("🚀 Bridge da san sang! Dang chuyen tiep du lieu...")

try:
    while True:
        if ser.in_waiting > 0:
            try:
                # Đọc 1 dòng từ ESP32 gửi lên
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                
                # Chỉ xử lý nếu là JSON hợp lệ (bắt đầu bằng { và kết thúc bằng })
                if line.startswith("{") and line.endswith("}"):
                    print(f"📤 Tu ESP32: {line}")
                    client.publish(MQTT_TOPIC_STATUS, line)
                elif line:
                    # In ra các dòng log khác (debug)
                    print(f"🔍 ESP32 Log: {line}")
                    
            except Exception as e:
                print(f"⚠️ Loi doc Serial: {e}")
                
        time.sleep(0.01) # Nghỉ ngắn để giảm tải CPU

except KeyboardInterrupt:
    print("\n🛑 Dung chuong trinh...")
    ser.close()
    client.loop_stop()
    client.disconnect()
    print("Da ngat ket noi an toan.")