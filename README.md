# 基于 STM32 的智能花盆系统

一套「感知 → 决策 → 执行 → 上云」全链路的智能植物养护系统。STM32F411 负责实时控制，ESP32-S3 负责联网上云，PC 端 Flask 提供可视化监控与远程控制，跑在 MCU 上的轻量随机森林模型实时诊断植物健康状态。

![platform](https://img.shields.io/badge/MCU-STM32F411RET6-blue) ![rtos](https://img.shields.io/badge/RTOS-FreeRTOS-green) ![ai](https://img.shields.io/badge/TinyML-RandomForest-orange) ![cloud](https://img.shields.io/badge/Cloud-OneNET%20MQTT-purple)

## 功能特性

- **多传感器融合采集**：土壤湿度（ADC）、光照强度（ADC）、空气温湿度（DHT11 单总线）
- **自动闭环控制**：土壤干 → 开水泵；光照弱 → 开补光灯；温度高 → 开风扇；光照强 → 舵机驱动遮阳帘
- **端上 AI 诊断**：随机森林（10 棵树）以纯 C 规则跑在 MCU 上，每 2s 推理一次，输出 健康 / 光照不足 / 温度异常 / 缺水预警，异常时蜂鸣器分级报警，支持手动静音
- **双模式运行**：自动模式（阈值闭环）/ 手动模式（串口指令），随时切换
- **三通道通信**：USART1 蓝牙 CLI、USART2 PC 串口、USART6 → ESP32-S3（JSON 一体化报文）
- **物联网上云**：ESP32 通过 MQTT 接入 OneNET 平台，5s 周期上报，本地 ST7789 TFT 同步显示
- **Web 可视化**：Flask + Chart.js 实时曲线、设备状态面板、网页按钮远程控制
- **OLED 本地交互**：4 种显示页面循环切换（温湿度 / 光照 / 土壤湿度 / AI 状态）

## 系统架构

```
┌──────────────┐  JSON   ┌──────────────┐   MQTT    ┌──────────────┐
│  STM32F411   │ USART6  │   ESP32-S3   │ ◄──────►  │ OneNET 云平台 │
│  (FreeRTOS)  │◄──────► │ (Arduino)    │           └──────────────┘
│              │ 115200  │  + ST7789 TFT│
│  传感器×3    │         └──────┬───────┘
│  执行器×5    │                │ Serial 透传
│  OLED 显示   │                ▼
│  随机森林 AI │         ┌──────────────┐
└──────────────┘         │  PC (Flask)  │
       ▲                 │  Web 监控页面 │
       │ 蓝牙/串口 CLI    └──────────────┘
       └── 手机蓝牙 App / 电脑串口助手
```

数据流：STM32 每 2s 通过 USART6 发送一体化 JSON → ESP32 解析后分三路：① 原样透传 PC；② 驱动 TFT；③ 组包上报 OneNET。反向控制：Web/蓝牙指令 → ESP32/STM32 解析执行。

## 硬件清单

| 模块 | 型号/说明 | 接口 | STM32 引脚 |
|---|---|---|---|
| 主控 | STM32F411-Nucleo | — | STM32F411RET6 |
| 土壤湿度传感器 | 电阻式，模拟电压输出 | ADC1_IN6 | PA6 |
| 光敏传感器 | 光敏电阻分压，模拟电压 | ADC1_IN0 | PA0 |
| 温湿度传感器 | DHT11，单总线 | GPIO + TIM2 us 延时 | PB5 |
| OLED 显示屏 | 0.96" SSD1306，4 线 SPI | SPI2 | PB6(RES) / PB7(DC) / PB12(CS) / PB10(SCK) / PB15(MOSI) |
| 补光灯 | LED（Nucleo 板载 LD2） | GPIO 低电平触发 | PA5 |
| 风扇 | 直流风扇 + 驱动 | GPIO | PC1 |
| 水泵 | 微型直流水泵 + 继电器 | GPIO | PA7 |
| 舵机（遮阳帘） | SG90，TIM1_CH1 PWM | PWM | PA8 |
| 蜂鸣器 | 有源蜂鸣器，低电平触发 | GPIO | PB1 |
| 按键 | 显示页面翻页 | GPIO 输入上拉 | PB0（Nucleo 用户键） |
| 蓝牙 | HC-05 类模块 | USART1 115200 | PA9(TX)/PA10(RX) |
| WiFi 网关 | ESP32-S3 + ST7789 TFT | USART6 115200 | PC6(TX)/PC7(RX) |

> 注：低电平触发指继电器/驱动模块低电平吸合；舵机 PWM 周期 20ms（TIM1 PSC=199, ARR=9999 @100MHz）。

## 软件架构（STM32 固件）

HAL + FreeRTOS（CMSIS-RTOS v2）多任务，任务间用二值/计数信号量 + 互斥锁同步：

| 任务 | 周期 | 职责 |
|---|---|---|
| `ADCTask` | 1s | 光照 ADC 采集（CH0）、补光灯 + 舵机自动控制 |
| `SWCTask` | 1s | 土壤湿度 ADC 采集（CH6，动态切通道）、水泵自动控制 |
| `DHT11Task` | 2s | DHT11 温湿度读取、风扇自动控制 |
| `ComTask` | 2s | 三路串口上报：蓝牙/PC 文本 + ESP32 JSON；ISR 安全延迟回复 |
| `OledTask` | ~ms 级 | OLED 4 页面刷新、按键翻页 |
| `AITask` | 50ms 轮询 | 随机森林推理（2s 一次）+ 蜂鸣器异步状态机 |

关键工程设计：

- **ADC 通道分时复用**：光照（CH0）与土壤（CH6）共用一个 ADC1，用二值信号量互斥，采完即切回默认通道
- **ISR 安全**：中断里只置标志（`cmd_resp_pending` / `buzzer_trigger`），实际发送/响铃全部放任务里做，杜绝在中断里延时死锁
- **蜂鸣器状态机**：响 150ms / 停 300ms 分级报警，BZOFF 紧急静音 30s，AI 状态恢复立即解除
- **串口互斥锁**：三个 UART 共享发送，防任务间踩踏

## AI 模型（TinyML）

随机森林（sklearn 训练，10 棵树，max_depth=5）直接导出为 C 语言 if-else 规则（`firmware/Core/Inc/random_forest.h`），10 棵树投票出诊断结果。训练准确率 96.67%，测试准确率 100%。输入：温度 / 空气湿度 / 土壤湿度 / 光照；输出：0 健康 / 1 光照不足 / 2 温度异常 / 3 缺水预警。零动态内存、零浮点依赖库，推理耗时微秒级。

## 串口指令表

指令经回车（`\r`/`\n`）终止，三条串口（蓝牙/PC/ESP32）均可下发：

| 指令 | 功能 | 备注 |
|---|---|---|
| `ON` / `OFF` | 切手动 / 自动模式 | 始终可用 |
| `AO` / `AF` | 风扇 开 / 关 | 仅手动模式 |
| `BO` / `BF` | 补光灯 开 / 关 | 仅手动模式 |
| `CO` / `CF` | 水泵 开 / 关 | 仅手动模式 |
| `SERVO_0` / `SERVO_90` / `SERVO_180` | 遮阳帘角度 | 仅手动模式 |
| `BUZZER` | 蜂鸣器测试响 3 声 | 仅手动模式 |
| `BZOFF` | 蜂鸣器紧急静音 | 始终可用 |

执行成功回复 `CMD_OK: <指令>`；自动模式下手动指令回复 `CMD_ERR: Need Manual Mode`。

## 数据协议

STM32 → ESP32（USART6，115200，2s 周期，`\r\n` 结尾）：

```json
{"temp":25.0,"humi":60.0,"soil":45,"light":1200.0,"ai":0,"fan":0,"bulb":1,"pump":0,"servo":90,"mode":0}
```

PC Web 控制指令经 ESP32 透传下发（`main.py` 里的 `cmd_map` 完成网页键名 → STM32 指令映射）。

## 目录结构

```
SmartFlowerPot/
├── firmware/                  # STM32F411 Keil MDK 工程（CubeMX 生成 + 手写应用层）
│   ├── Core/                  #   主程序：main.c（任务+CLI+控制逻辑）、random_forest.h
│   ├── Hardware/              #   外设驱动：DHT11、OLED(SSD1306)
│   ├── Drivers/               #   HAL + CMSIS（已精简：仅保留 F411 相关文件）
│   ├── Middlewares/           #   FreeRTOS 源码
│   ├── MDK-ARM/               #   Keil 工程文件（.uvprojx/.uvoptx，编译产物已剔除）
│   ├── buzzertest/            #   蜂鸣器单模块测试工程（开发过程留档）
│   └── 111FlowerProject.ioc   #   STM32CubeMX 配置文件
├── esp32/
│   └── WIFI_MQTT.ino          # ESP32-S3 Arduino 工程：WiFi/MQTT/TFT/串口透传
├── pc-side/
│   ├── main.py                # Flask 主服务：串口读取线程 + 监测/控制页面 + API
│   ├── flowerpot_ui.py        # tkinter 桌面控制面板：串口直连 STM32，全指令按钮
│   ├── server.py              # 早期版 CORS 数据服务（留档）
│   └── dashboard.html         # 独立前端页面（与 main.py 内嵌版一致）
├── docs/                      # 开发过程文档
│   ├── ESP32-S3开发方案.txt    #   ESP32 端接线/配云/联调完整方案
│   ├── STM32搭建流程参考.txt   #   STM32 端搭建流程（含引脚分配）
│   └── 系统照片.png            #   温度控制风扇功能流程图
└── README.md
```

## 快速开始

### 1. STM32 固件

- 工具：Keil MDK V5.32+（或任何 ARM Compiler V5/V6）
- 打开 `firmware/MDK-ARM/111FlowerProject.uvprojx`，编译后烧录（ST-Link）
- 串口：USART1（PA9/PA10，115200）接蓝牙模块或 USB 转 TTL 查看 CLI
- 也可以用 CubeMX 打开 `111FlowerProject.ioc` 重新生成代码

### 2. ESP32 网关

- 工具：Arduino IDE，安装 ESP32-S3 开发板支持
- 依赖库：`PubSubClient`、`ArduinoJson`、`Adafruit GFX`、`Adafruit ST7789`
- **先改凭据**：打开 `esp32/WIFI_MQTT.ino`，把 `YOUR_WIFI_SSID` / `YOUR_WIFI_PASSWORD` / `YOUR_PRODUCT_ID` / `YOUR_DEVICE_ID` / `YOUR_ONENET_AUTH_KEY` 换成自己的（WiFi 密码和 OneNET token 属敏感信息，仓库中已做占位符处理，请勿把真实凭据提交到公开仓库）
- 烧录后：Serial1(GPIO17/18) ↔ STM32 USART6，Serial(USB) ↔ PC

### 3. PC 监控端

两种上位机任选：

**A. Flask Web 监控（经 ESP32 透传，可远程控制）**

```bash
pip install flask pyserial
python pc-side/main.py        # 默认 COM6@115200，按实际端口改 COM_PORT
# 浏览器打开 http://127.0.0.1:5000
```

**B. tkinter 桌面控制面板（串口直连 STM32）**

```bash
pip install pyserial
python pc-side/flowerpot_ui.py   # 界面内选择串口与波特率后连接
```

## 测试结果

- 土壤湿度检测误差 < 5%，浇水阈值逻辑符合预期
- 光照检测精度约 50 lux，补光灯/遮阳帘响应正确
- 双串口 + 蓝牙三通道数据稳定，指令执行准确
- 系统联调整体运行稳定

## 已知限制 / 后续方向

- 光照标定系数 `CONVERSION_FACTOR` 需按实际传感器校准
- OneNET token 有有效期，需定期更换
- 可扩展：多植物个性化阈值、水肥一体化、接入更多云平台

## 安全说明

本仓库已移除所有敏感信息：WiFi 密码、OneNET 设备 token、真实姓名学号等均已替换为占位符或剔除。克隆后请自行到 `esp32/WIFI_MQTT.ino` 顶部配置区填写你自己的凭据。

## 许可

课程设计项目代码，仅供学习交流。
