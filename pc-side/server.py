from flask import Flask, jsonify
from flask_cors import CORS
import serial

app = Flask(__name__)
CORS(app)

ser = None
# 清空初始模拟值，仅声明空容器，无预设假数据
cache = {
    "ADCL": None,
    "ADC_H": None,
    "DHT11_H": None,
    "DHT11_T": None
}
# 标记是否已接收到硬件真实数据
has_real_data = False

# 延迟初始化串口
def init_serial():
    global ser
    if ser is not None and ser.is_open:
        return
    try:
        ser = serial.Serial(port="COM6", baudrate=115200, timeout=0.5)
        print("✅ 串口COM6打开成功，等待ESP32硬件数据上报")
    except Exception as e:
        print(f"❌ 串口打开失败: {str(e)}")
        print("提示：关闭Arduino串口监视器/串口助手，或重新插拔ESP32 USB线")

@app.route('/getdata')
def getdata():
    global has_real_data
    init_serial()
    # 串口就绪则持续读取硬件串口数据
    if ser and ser.is_open:
        while ser.in_waiting > 0:
            line = ser.readline().decode("utf-8", errors="ignore")
            if "光照ADCL:" in line:
                parts = line.split("|")
                # 仅由ESP32串口硬件数据赋值，无任何预设模拟值
                cache["ADCL"] = float(parts[0].split(":")[1].replace("Lux", "").strip())
                cache["ADC_H"] = float(parts[1].split(":")[1].replace("%RH", "").strip())
                cache["DHT11_H"] = float(parts[2].split(":")[1].replace("%RH", "").strip())
                cache["DHT11_T"] = float(parts[3].split(":")[1].replace("℃", "").strip())
                has_real_data = True
                print("📡 收到ESP32真实传感器数据:", cache)
    # 组装返回数据，未收到硬件数据时返回空值，前端显示--
    output = []
    for k, v in cache.items():
        output.append({"identifier": k, "value": v})
    return jsonify({"code": 0, "data": output})

if __name__ == '__main__':
    app.run(host="127.0.0.1", port=5000, debug=True)