import serial
import json
import threading
import time
import logging
from flask import Flask, jsonify, render_template_string, request

# ====================== 串口基础配置 ======================
COM_PORT = "COM6"        # ESP32对应串口
BAUD_RATE = 115200       # 波特率和ESP32、STM32统一
# 传感器全局存储：对应温湿度、土壤、光照
sensor_data = {
    "ADCL": 0.0,    # 光照强度
    "ADC_H": 0.0,   # 土壤湿度
    "DHT11_H": 0.0, # 空气湿度
    "DHT11_T": 0.0  # 空气温度
}
# 历史数据缓存，用于图表绘制
history_data = {
    "time": [],
    "temp": [],
    "dht_humi": [],
    "soil_humi": [],
    "light": []
}
MAX_HISTORY = 25  # 最大缓存点数，防止内存溢出
ser = None         # 串口对象全局变量

# 设备综合状态结构体，对接STM32上报JSON所有设备状态
device_status = {
    "mode": "auto",       # 运行模式 auto自动 / manual手动
    "fan": "off",         # 风扇状态 on开 / off关
    "light": "off",       # 补光灯状态（已做自动模式电平反转）
    "pump": "off",        # 水泵状态（已做自动模式电平反转）
    "servo": "90",        # 舵机角度 0/90/180
    "buzzer": "off",      # 蜂鸣器状态
    "ai_state": 0         # AI植物健康状态：0健康 1光照异常 2温度异常 3缺水
}

# Flask服务初始化
app = Flask(__name__)
# 关闭Flask控制台冗余日志
log = logging.getLogger('werkzeug')
log.setLevel(logging.ERROR)

# ====================== 串口初始化函数 ======================
def serial_init():
    global ser
    try:
        # 打开串口，超时1秒
        ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=1)
        print(f"串口 {COM_PORT} 连接成功，等待ESP32 JSON数据...")
    except Exception as e:
        print("串口打开失败：", e)

# ====================== 串口接收解析线程（核心数据处理） ======================
def serial_read_task():
    global sensor_data, history_data, device_status
    while True:
        # 串口未就绪则循环等待
        if ser is None or not ser.is_open:
            time.sleep(1)
            continue
        try:
            # 读取一行串口数据，去除首尾空格换行
            line = ser.readline().decode("utf-8").strip()
            if not line:
                continue
            # 解析STM32标准JSON报文
            data_json = json.loads(line)
            print("收到STM32 JSON:", data_json)

            # 1. 基础传感器数值赋值
            sensor_data["DHT11_T"] = round(float(data_json["temp"]), 1)
            sensor_data["DHT11_H"] = round(float(data_json["humi"]), 1)
            sensor_data["ADC_H"] = round(float(data_json["soil"]), 1)
            sensor_data["ADCL"] = round(float(data_json["light"]), 1)

            # 2. 拆分JSON原始硬件电平
            raw_mode = data_json["mode"]
            raw_fan = data_json["fan"]
            raw_bulb = data_json["bulb"]
            raw_pump = data_json["pump"]
            raw_servo = str(data_json["servo"])
            raw_ai = data_json["ai"]

            # 3. 运行模式判定：mode=1手动，mode=0自动
            device_status["mode"] = "manual" if raw_mode == 1 else "auto"
            # 风扇固定逻辑：1开启 0关闭，全程不反转
            device_status["fan"] = "on" if raw_fan == 1 else "off"
            # 舵机直接赋值角度
            device_status["servo"] = raw_servo
            # 存储AI健康推理数值
            device_status["ai_state"] = raw_ai

            # 4. 关键逻辑：区分手动/自动，补光灯、水泵电平反转规则
            if device_status["mode"] == "manual":
                # 手动模式：正向逻辑 1=开 0=关
                device_status["light"] = "on" if raw_bulb == 1 else "off"
                device_status["pump"] = "on" if raw_pump == 1 else "off"
            else:
                # 自动模式：bulb、pump电平反向 0=开 1=关
                device_status["light"] = "on" if raw_bulb == 0 else "off"
                device_status["pump"] = "on" if raw_pump == 0 else "off"

            # 5. 存入历史绘图数据
            now_time = time.strftime("%H:%M:%S")
            history_data["time"].append(now_time)
            history_data["temp"].append(sensor_data["DHT11_T"])
            history_data["dht_humi"].append(sensor_data["DHT11_H"])
            history_data["soil_humi"].append(sensor_data["ADC_H"])
            history_data["light"].append(sensor_data["ADCL"])

            # 超出最大点数，剔除最早数据
            if len(history_data["time"]) > MAX_HISTORY:
                for key in history_data:
                    history_data[key].pop(0)
        except json.JSONDecodeError:
            # 非标准JSON杂波数据直接跳过，不崩溃
            continue
        except Exception as err:
            # 其余异常静默跳过，保证线程持续运行
            continue

# ====================== 串口指令下发函数（网页控制→ESP32→STM32） ======================
def send_uart_cmd(cmd):
    global ser
    if ser is not None and ser.is_open:
        send_str = cmd + "\n"
       ser.write(send_str.encode("utf-8"))
        print(f"下发控制指令: {cmd}")
        return True
    return False

# ====================== 后端API接口：设备控制 ======================
@app.route("/api/control", methods=["POST"])
def control_device():
    global device_status
    req = request.get_json()
    cmd = req.get("cmd", "")
    if not cmd:
        return jsonify({"code":400,"msg":"指令为空"})
    # 网页前端指令 ↔ STM32识别串口指令映射表
    cmd_map = {
        "mode_manual": "ON",
        "mode_auto": "OFF",
        "fan_on": "AO",
        "fan_off": "AF",
        "light_on": "BO",
        "light_off": "BF",
        "pump_on": "CO",
        "pump_off": "CF",
        "servo_0": "SERVO_0",
        "servo_90": "SERVO_90",
        "servo_180": "SERVO_180",
        "buzzer_on": "BUZZER",
        "buzzer_off": "BZOFF"
    }
    if cmd not in cmd_map:
        return jsonify({"code":400,"msg":"无效指令"})
    # 下发串口指令
    send_uart_cmd(cmd_map[cmd])
    # 前端临时预刷新状态，后续会被STM32真实上报状态覆盖
    if cmd == "mode_manual": device_status["mode"] = "manual"
    if cmd == "mode_auto": device_status["mode"] = "auto"
    if cmd == "fan_on": device_status["fan"] = "on"
    if cmd == "fan_off": device_status["fan"] = "off"
    if cmd == "light_on": device_status["light"] = "on"
    if cmd == "light_off": device_status["light"] = "off"
    if cmd == "pump_on": device_status["pump"] = "on"
    if cmd == "pump_off": device_status["pump"] = "off"
    if cmd in ["servo_0","servo_90","servo_180"]: device_status["servo"] = cmd.split("_")[1]
    if cmd == "buzzer_on": device_status["buzzer"] = "on"
    if cmd == "buzzer_off": device_status["buzzer"] = "off"

    return jsonify({"code":200,"msg":"指令发送成功","status":device_status})

# ====================== 后端API接口：传感器+设备状态总查询 ======================
@app.route("/api/sensor")
def get_sensor():
    # 判断传感器是否有有效数据，判定在线离线
    any_valid = any(v > 0 for v in sensor_data.values())
    return jsonify({
        "now": time.strftime("%Y/%m/%d %H:%M:%S"),
        "status": "normal" if any_valid else "disconnect",
        "data": sensor_data,
        "device": device_status
    })

# ====================== 后端API接口：历史绘图数据 ======================
@app.route("/api/history")
def get_history():
    return jsonify(history_data)

# ====================== 页面1：数据监测主页 ======================
@app.route("/")
def page_data():
    page_html = """
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>智能花盆 - 数据监测页</title>
    <!-- 引入Chart.js绘制折线图 -->
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: "Microsoft YaHei", sans-serif;
        }
        body {
            background-color: #0f1722;
            color: #e2e8f0;
            padding: 20px;
        }
        .header-title {
            text-align: center;
            font-size: 26px;
            font-weight: 600;
            margin-bottom: 20px;
            color: #ffffff;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 8px;
        }
        .header-title::before {content: "🪴";}

        /* 顶部导航栏样式 */
        .nav-bar{
            display:flex;
            justify-content:center;
            gap:12px;
            margin-bottom:20px;
        }
        .nav-btn{
            padding:10px 24px;
            border:none;
            border-radius:6px;
            font-size:16px;
            cursor:pointer;
        }
        .nav-active{
            background:#2563eb;
            color:#fff;
        }
        .nav-normal{
            background:#334155;
            color:#cbd5e1;
        }

        /* 设备在线状态栏 */
        .status-bar {
            background-color: #1e293b;
            border-radius: 8px;
            padding: 12px 20px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 20px;
        }
        .status-normal {color: #4ade80;font-size: 18px;display: flex;align-items: center;gap:6px;}
        .status-normal::before {content: "●";}
        .status-error {color: #f87171;font-size: 18px;display: flex;align-items: center;gap:6px;}
        .status-error::before {content: "●";}
        .time-text {color: #cbd5e1;font-size:16px;}

        /* 硬件总状态面板：6等分网格（模式、风扇、灯光、水泵、舵机、AI植物状态） */
        .hardware-status{
            background:#1e293b;
            border-radius:8px;
            padding:16px;
            margin-bottom:20px;
            display:grid;
            grid-template-columns: repeat(6,1fr);
            gap:12px;
            text-align:center;
        }
        .status-item{
            padding:10px;
            border-radius:6px;
            background:#334155;
        }
        .status-label{
            font-size:14px;
            color:#94a3b8;
            margin-bottom:6px;
        }
        /* 通用状态颜色 */
        .stat-on{color:#4ade80;font-weight:bold;}    /* 开启-绿色 */
        .stat-off{color:#64748b;font-weight:bold;}   /* 关闭-灰色 */
        .stat-manual{color:#60a5fa;font-weight:bold;}/* 手动-蓝色 */
        .stat-auto{color:#fb923c;font-weight:bold;}   /* 自动-橙色 */
        /* AI健康状态专属颜色 */
        .ai-health{color:#4ade80;font-weight:bold;}     /* 健康绿 */
        .ai-light-err{color:#facc15;font-weight:bold;}    /* 光照异常黄 */
        .ai-temp-err{color:#f87171;font-weight:bold;}     /* 温度异常红 */
        .ai-water-err{color:#38bdf8;font-weight:bold;}   /* 缺水浅蓝 */

        /* 传感器数值卡片布局 */
        .data-row {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 16px;
            margin:20px 0;
        }
        .data-card {
            background-color: #1e293b;
            border-radius: 10px;
            padding: 20px 16px;
            text-align: center;
        }
        .card-label {font-size: 16px;color: #94a3b8;margin-bottom:10px;}
        .card-value {font-size: 32px;font-weight: bold;}
        .temp-color {color:#f87171;}
        .hum-dht-color {color:#60a5fa;}
        .soil-color {color:#c084fc;}
        .light-color {color:#facc15;}

        /* 图表区域双列布局 */
        .chart-grid {
            display: grid;
            grid-template-columns: repeat(2, 1fr);
            gap: 20px;
        }
        .chart-box {
            background-color: #1e293b;
            border-radius:10px;
            padding:20px;
        }
        canvas {
            width:100% !important;
            height:300px !important;
        }
    </style>
</head>
<body>
    <div class="header-title">智能花盆控制系统 v1.1</div>

    <div class="nav-bar">
        <button class="nav-btn nav-active">📊 数据监测页</button>
        <button class="nav-btn nav-normal" onclick="location.href='/control'">🎛️ 设备控制页</button>
    </div>

    <div class="status-bar">
        <div class="status-error" id="stateText">设备未连接</div>
        <div class="time-text" id="nowTime">2026/6/23 19:41:59</div>
    </div>

    <!-- 硬件实时状态展示区 -->
    <div class="hardware-status">
        <div class="status-item">
            <div class="status-label">运行模式</div>
            <div id="showMode" class="stat-auto">自动</div>
        </div>
        <div class="status-item">
            <div class="status-label">风扇状态</div>
            <div id="showFan" class="stat-off">关闭</div>
        </div>
        <div class="status-item">
            <div class="status-label">补光灯状态</div>
            <div id="showLight" class="stat-off">关闭</div>
        </div>
        <div class="status-item">
            <div class="status-label">水泵状态</div>
            <div id="showPump" class="stat-off">关闭</div>
        </div>
        <div class="status-item">
            <div class="status-label">舵机角度</div>
            <div id="showServo" style="color:#fff;font-weight:bold;">90°</div>
        </div>
        <div class="status-item">
            <div class="status-label">植物健康状态</div>
            <div id="showAiState" class="ai-health">健康</div>
        </div>
    </div>

    <!-- 传感器实时数值卡片 -->
    <div class="data-row">
        <div class="data-card">
            <div class="card-label">空气温度 DHT11_T</div>
            <div class="card-value temp-color" id="valTemp">0.0 ℃</div>
        </div>
        <div class="data-card">
            <div class="card-label">空气湿度 DHT11_H</div>
            <div class="card-value hum-dht-color" id="valDhtHum">0.0 %RH</div>
        </div>
        <div class="data-card">
            <div class="card-label">土壤湿度 ADC_H</div>
            <div class="card-value soil-color" id="valSoilHum">0.0 %RH</div>
        </div>
        <div class="data-card">
            <div class="card-label">光照强度 ADCL</div>
            <div class="card-value light-color" id="valLight">0.0 lx</div>
        </div>
    </div>

    <!-- 四组传感器折线图表 -->
    <div class="chart-grid">
        <div class="chart-box"><canvas id="chartTemp"></canvas></div>
        <div class="chart-box"><canvas id="chartDhtHum"></canvas></div>
        <div class="chart-box"><canvas id="chartSoilHum"></canvas></div>
        <div class="chart-box"><canvas id="chartLight"></canvas></div>
    </div>

<script>
// 初始化温度图表
const ctxTemp = document.getElementById('chartTemp').getContext('2d');
const chartTemp = new Chart(ctxTemp, {
    type: 'line',
    data: {labels: [], datasets: [{label:'温度 ℃', borderColor:'#f87171', backgroundColor:'rgba(248,113,113,0.1)', tension:0.3, fill:false, borderWidth:2}]},
    options:{responsive:true, maintainAspectRatio:false, scales:{x:{grid:{color:'rgba(255,255,255,0.08)'},ticks:{color:'#94a3b8'}},y:{grid:{color:'rgba(255,255,255,0.08)'},ticks:{color:'#94a3b8'}}},plugins:{legend:{labels:{color:'#cbd5e1'}}}}
});
// 初始化空气湿度图表
const ctxDhtHum = document.getElementById('chartDhtHum').getContext('2d');
const chartDhtHum = new Chart(ctxDhtHum, {
    type: 'line',
    data: {labels: [], datasets: [{label:'空气湿度 %RH', borderColor:'#60a5fa', backgroundColor:'rgba(96,165,250,0.1)', tension:0.3, fill:false, borderWidth:2}]},
    options:{responsive:true, maintainAspectRatio:false, scales:{x:{grid:{color:'rgba(255,255,255,0.08)'},ticks:{color:'#94a3b8'}},y:{grid:{color:'rgba(255,255,255,0.08)'},ticks:{color:'#94a3b8'}}},plugins:{legend:{labels:{color:'#cbd5e1'}}}}
});
// 初始化土壤湿度图表
const ctxSoilHum = document.getElementById('chartSoilHum').getContext('2d');
const chartSoilHum = new Chart(ctxSoilHum, {
    type: 'line',
    data: {labels: [], datasets: [{label:'土壤湿度 %RH', borderColor:'#c084fc', backgroundColor:'rgba(192,132,252,0.1)', tension:0.3, fill:false, borderWidth:2}]},
    options:{responsive:true, maintainAspectRatio:false, scales:{x:{grid:{color:'rgba(255,255,255,0.08)'},ticks:{color:'#94a3b8'}},y:{grid:{color:'rgba(255,255,255,0.08)'},ticks:{color:'#94a3b8'}}},plugins:{legend:{labels:{color:'#cbd5e1'}}}}
});
// 初始化光照强度图表
const ctxLight = document.getElementById('chartLight').getContext('2d');
const chartLight = new Chart(ctxLight, {
    type: 'line',
    data: {labels: [], datasets: [{label:'光照 lx', borderColor:'#facc15', backgroundColor:'rgba(250,204,21,0.1)', tension:0.3, fill:false, borderWidth:2}]},
    options:{responsive:true, maintainAspectRatio:false, scales:{x:{grid:{color:'rgba(255,255,255,0.08)'},ticks:{color:'#94a3b8'}},y:{grid:{color:'rgba(255,255,255,0.08)'},ticks:{color:'#94a3b8'}}},plugins:{legend:{labels:{color:'#cbd5e1'}}}}
});

/**
 * AI状态数值转文字+样式类
 * @param {number} val ai原始数值 0/1/2/3
 * @returns {Object} {text:显示文字, cls:css类名}
 */
function parseAiState(val){
    switch(val){
        case 0: return {text:"健康", cls:"ai-health"};
        case 1: return {text:"光照异常", cls:"ai-light-err"};
        case 2: return {text:"温度异常", cls:"ai-temp-err"};
        case 3: return {text:"缺水", cls:"ai-water-err"};
        default: return {text:"未知状态", cls:"stat-off"};
    }
}

/**
 * 定时刷新页面所有数据、状态、图表
 */
function updatePage(){
    fetch("/api/sensor")
    .then(res=>res.json())
    .then(res=>{
        // 更新系统时间
        document.getElementById("nowTime").innerText = res.now;
        const stateDom = document.getElementById("stateText");
        // 在线离线状态判断
        if(res.status === "normal"){
            stateDom.innerText = "设备正常在线";
            stateDom.className = "status-normal";
        }else{
            stateDom.innerText = "设备未连接";
            stateDom.className = "status-error";
        }
        const d = res.data;
        const dev = res.device;
        // 传感器数值回填
        document.getElementById("valTemp").innerText = d.DHT11_T + " ℃";
        document.getElementById("valDhtHum").innerText = d.DHT11_H + " %RH";
        document.getElementById("valSoilHum").innerText = d.ADC_H + " %RH";
        document.getElementById("valLight").innerText = d.ADCL + " lx";

        // 运行模式文字+配色
        const modeDom = document.getElementById("showMode");
        if(dev.mode === "manual"){
            modeDom.innerText = "手动";
            modeDom.className = "stat-manual";
        }else{
            modeDom.innerText = "自动";
            modeDom.className = "stat-auto";
        }

        // 风扇状态
        const fanDom = document.getElementById("showFan");
        fanDom.innerText = dev.fan==="on"?"开启":"关闭";
        fanDom.className = dev.fan==="on"?"stat-on":"stat-off";

        // 补光灯状态（后端已处理自动反转逻辑）
        const lightDom = document.getElementById("showLight");
        lightDom.innerText = dev.light==="on"?"开启":"关闭";
        lightDom.className = dev.light==="on"?"stat-on":"stat-off";

        // 水泵状态（后端已处理自动反转逻辑）
        const pumpDom = document.getElementById("showPump");
        pumpDom.innerText = dev.pump==="on"?"开启":"关闭";
        pumpDom.className = dev.pump==="on"?"stat-on":"stat-off";

        // 舵机角度
        document.getElementById("showServo").innerText = dev.servo + "°";

        // AI植物健康状态渲染
        let aiInfo = parseAiState(dev.ai_state);
        let aiDom = document.getElementById("showAiState");
        aiDom.innerText = aiInfo.text;
        aiDom.className = aiInfo.cls;
    }).catch(()=>{
        // 接口请求失败兜底
        document.getElementById("stateText").innerText = "接口请求失败";
        document.getElementById("stateText").className = "status-error";
    })

    // 更新历史折线图数据
    fetch("/api/history")
    .then(res=>res.json())
    .then(hist=>{
        chartTemp.data.labels = hist.time; chartTemp.data.datasets[0].data = hist.temp; chartTemp.update();
        chartDhtHum.data.labels = hist.time; chartDhtHum.data.datasets[0].data = hist.dht_humi; chartDhtHum.update();
        chartSoilHum.data.labels = hist.time; chartSoilHum.data.datasets[0].data = hist.soil_humi; chartSoilHum.update();
        chartLight.data.labels = hist.time; chartLight.data.datasets[0].data = hist.light; chartLight.update();
    })
}

// 1.5秒循环刷新
setInterval(updatePage, 1500);
// 页面加载立即执行一次刷新
updatePage();
</script>
</body>
</html>
    """
    return render_template_string(page_html)

# ====================== 页面2：设备控制页面 ======================
@app.route("/control")
def page_control():
    ctrl_html = """
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>智能花盆 - 设备控制页</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
            font-family: "Microsoft YaHei", sans-serif;
        }
        body {
            background-color: #0f1722;
            color: #e2e8f0;
            padding: 20px;
        }
        .header-title {
            text-align: center;
            font-size: 26px;
            font-weight: 600;
            margin-bottom: 20px;
            color: #ffffff;
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 8px;
        }
        .header-title::before {content: "🪴";}

        .nav-bar{
            display:flex;
            justify-content:center;
            gap:12px;
            margin-bottom:20px;
        }
        .nav-btn{
            padding:10px 24px;
            border:none;
            border-radius:6px;
            font-size:16px;
            cursor:pointer;
        }
        .nav-active{
            background:#2563eb;
            color:#fff;
        }
        .nav-normal{
            background:#334155;
            color:#cbd5e1;
        }

        .status-bar {
            background-color: #1e293b;
            border-radius: 8px;
            padding: 12px 20px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 20px;
        }
        .status-normal {color: #4ade80;font-size: 18px;display: flex;align-items: center;gap:6px;}
        .status-normal::before {content: "●";}
        .status-error {color: #f87171;font-size: 18px;display: flex;align-items: center;gap:6px;}
        .status-error::before {content: "●";}
        .time-text {color: #cbd5e1;font-size:16px;}

        .hardware-status{
            background:#1e293b;
            border-radius:8px;
            padding:16px;
            margin-bottom:20px;
            display:grid;
            grid-template-columns: repeat(6,1fr);
            gap:12px;
            text-align:center;
        }
        .status-item{
            padding:10px;
            border-radius:6px;
            background:#334155;
        }
        .status-label{
            font-size:14px;
            color:#94a3b8;
            margin-bottom:6px;
        }
        .stat-on{color:#4ade80;font-weight:bold;}
        .stat-off{color:#64748b;font-weight:bold;}
        .stat-manual{color:#60a5fa;font-weight:bold;}
        .stat-auto{color:#fb923c;font-weight:bold;}
        .ai-health{color:#4ade80;font-weight:bold;}
        .ai-light-err{color:#facc15;font-weight:bold;}
        .ai-temp-err{color:#f87171;font-weight:bold;}
        .ai-water-err{color:#38bdf8;font-weight:bold;}

        .control-panel {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 15px;
            margin-bottom:20px;
        }
        .control-group {
            background:#1e293b;
            border-radius:8px;
            padding:16px;
        }
        .group-title {
            font-size:16px;
            margin-bottom:12px;
            color:#bcccdc;
            border-bottom:1px solid #334155;
            padding-bottom:6px;
        }
        .btn-row {
            display:flex;
            flex-direction:column;
            gap:10px;
        }
        button {
            padding:14px 8px;
            border:none;
            border-radius:6px;
            font-size:17px;
            font-weight:bold;
            cursor:pointer;
        }
        .btn-green {background:#22c55e;color:#fff;}
        .btn-red {background:#ef4444;color:#fff;}
        .btn-orange {background:#f97316;color:#fff;}
        .btn-purple {background:#a855f7;color:#fff;}
        .btn-gray {background:#475569;color:#fff;}
        .servo-row {
            display:flex;
            gap:8px;
        }
        .servo-row button {flex:1;padding:12px 0;}
    </style>
</head>
<body>
    <div class="header-title">智能花盆控制系统 v1.1</div>

    <div class="nav-bar">
        <button class="nav-btn nav-normal" onclick="location.href='/'">📊 数据监测页</button>
        <button class="nav-btn nav-active">🎛️ 设备控制页</button>
    </div>

    <div class="status-bar">
        <div class="status-error" id="stateText">设备未连接</div>
        <div class="time-text" id="nowTime">2026/6/23 19:41:59</div>
    </div>

    <!-- 控制页顶部硬件状态总览 -->
    <div class="hardware-status">
        <div class="status-item">
            <div class="status-label">运行模式</div>
            <div id="ctrlMode" class="stat-auto">自动</div>
        </div>
        <div class="status-item">
            <div class="status-label">风扇</div>
            <div id="ctrlFan" class="stat-off">关闭</div>
        </div>
        <div class="status-item">
            <div class="status-label">补光灯</div>
            <div id="ctrlLight" class="stat-off">关闭</div>
        </div>
        <div class="status-item">
            <div class="status-label">水泵</div>
            <div id="ctrlPump" class="stat-off">关闭</div>
        </div>
        <div class="status-item">
            <div class="status-label">舵机角度</div>
            <div id="ctrlServo" style="color:#fff;font-weight:bold;">90°</div>
        </div>
        <div class="status-item">
            <div class="status-label">植物健康状态</div>
            <div id="ctrlAiState" class="ai-health">健康</div>
        </div>
    </div>

    <!-- 设备控制按钮区域 -->
    <div class="control-panel">
        <div class="control-group">
            <div class="group-title">模式控制</div>
            <div class="btn-row">
                <button class="btn-green" onclick="sendCtrl('mode_manual')">ON 手动模式</button>
                <button class="btn-red" onclick="sendCtrl('mode_auto')">OFF 自动模式</button>
            </div>
        </div>
        <div class="control-group">
            <div class="group-title">风扇控制</div>
            <div class="btn-row">
                <button class="btn-green" onclick="sendCtrl('fan_on')">AO 开启风扇</button>
                <button class="btn-red" onclick="sendCtrl('fan_off')">AF 关闭风扇</button>
            </div>
        </div>
        <div class="control-group">
            <div class="group-title">补光灯控制</div>
            <div class="btn-row">
                <button class="btn-green" onclick="sendCtrl('light_on')">BO 开启补光</button>
                <button class="btn-red" onclick="sendCtrl('light_off')">BF 关闭补光</button>
            </div>
        </div>
        <div class="control-group">
            <div class="group-title">水泵控制</div>
            <div class="btn-row">
                <button class="btn-green" onclick="sendCtrl('pump_on')">CO 开启水泵</button>
                <button class="btn-red" onclick="sendCtrl('pump_off')">CF 关闭水泵</button>
            </div>
        </div>

        <div class="control-group" style="grid-column: span 2;">
            <div class="group-title">舵机控制(遮光帘)</div>
            <div class="servo-row">
                <button class="btn-orange" onclick="sendCtrl('servo_0')">SERVO_0 全开</button>
                <button class="btn-orange" onclick="sendCtrl('servo_90')">SERVO_90 半开</button>
                <button class="btn-orange" onclick="sendCtrl('servo_180')">SERVO_180 全关</button>
            </div>
        </div>
        <div class="control-group">
            <div class="group-title">蜂鸣器控制</div>
            <div class="btn-row">
                <button class="btn-purple" onclick="sendCtrl('buzzer_on')">BUZZER 测试蜂鸣</button>
                <button class="btn-gray" onclick="sendCtrl('buzzer_off')">BZOFF 关闭蜂鸣</button>
            </div>
        </div>
        <div class="control-group">
            <div class="group-title">快捷操作</div>
            <div class="btn-row">
                <button class="btn-orange" onclick="quickAllOn()">全部开启</button>
            </div>
        </div>
    </div>

<script>
/**
 * 发送控制指令封装
 * @param {string} cmd 前端预设指令key
 */
function sendCtrl(cmd){
    fetch("/api/control",{
        method:"POST",
        headers:{"Content-Type":"application/json"},
        body:JSON.stringify({cmd:cmd})
    }).then(res=>res.json()).then(ret=>{
        if(ret.code===200) console.log("指令发送成功:",cmd);
        else alert("指令失败:"+ret.msg);
    })
}
// 一键开启风扇、灯光、水泵、蜂鸣器
function quickAllOn(){
    sendCtrl("fan_on");
    sendCtrl("light_on");
    sendCtrl("pump_on");
    sendCtrl("buzzer_on");
}

/**
 * AI状态解析，和数据页共用一套逻辑，保证显示统一
 */
function parseAiState(val){
    switch(val){
        case 0: return {text:"健康", cls:"ai-health"};
        case 1: return {text:"光照异常", cls:"ai-light-err"};
        case 2: return {text:"温度异常", cls:"ai-temp-err"};
        case 3: return {text:"缺水", cls:"ai-water-err"};
        default: return {text:"未知状态", cls:"stat-off"};
    }
}

/**
 * 控制页状态定时刷新
 */
function updateStatus(){
    fetch("/api/sensor")
    .then(res=>res.json())
    .then(res=>{
        document.getElementById("nowTime").innerText = res.now;
        const stateDom = document.getElementById("stateText");
        if(res.status === "normal"){
            stateDom.innerText = "设备正常在线";
            stateDom.className = "status-normal";
        }else{
            stateDom.innerText = "设备未连接";
            stateDom.className = "status-error";
        }
        const dev = res.device;

        // 模式显示
        const modeDom = document.getElementById("ctrlMode");
        if(dev.mode === "manual"){
            modeDom.innerText = "手动";
            modeDom.className = "stat-manual";
        }else{
            modeDom.innerText = "自动";
            modeDom.className = "stat-auto";
        }

        // 风扇
        const fanDom = document.getElementById("ctrlFan");
        fanDom.innerText = dev.fan==="on"?"开启":"关闭";
        fanDom.className = dev.fan==="on"?"stat-on":"stat-off";

        // 补光灯
        const lightDom = document.getElementById("ctrlLight");
        lightDom.innerText = dev.light==="on"?"开启":"关闭";
        lightDom.className = dev.light==="on"?"stat-on":"stat-off";

        // 水泵
        const pumpDom = document.getElementById("ctrlPump");
        pumpDom.innerText = dev.pump==="on"?"开启":"关闭";
        pumpDom.className = dev.pump==="on"?"stat-on":"stat-off";

        // 舵机
        document.getElementById("ctrlServo").innerText = dev.servo + "°";

        // AI健康状态
        let aiInfo = parseAiState(dev.ai_state);
        let aiDom = document.getElementById("ctrlAiState");
        aiDom.innerText = aiInfo.text;
        aiDom.className = aiInfo.cls;
    }).catch(()=>{
        document.getElementById("stateText").innerText = "接口请求失败";
        document.getElementById("stateText").className = "status-error";
    })
}
// 循环刷新
setInterval(updateStatus,1500);
updateStatus();
</script>
</body>
</html>
    """
    return render_template_string(ctrl_html)

# ====================== 程序入口 ======================
if __name__ == "__main__":
    # 初始化串口
    serial_init()
    # 启动串口接收后台守护线程
    read_thread = threading.Thread(target=serial_read_task, daemon=True)
    read_thread.start()
    # 启动Flask网页服务，本地访问
    app.run(host="127.0.0.1", port=5000, debug=False)