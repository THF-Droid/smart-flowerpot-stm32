#include <Arduino.h>
#include "WiFi.h"
#include "PubSubClient.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Wire.h>
#include <ArduinoJson.h>

// ===== WiFi配置（请替换为你自己的信息，切勿提交真实密码）=====
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";

// ===== OneNET MQTT配置 =====
// 说明：在 OneNET 开发者中心创建产品与设备后，从设备详情页复制
// product_id / device_id，并使用平台提供的 Token 生成工具得到 auth_key
// （形如 version=2018-10-31&res=products%2Fxxx%2Fdevices%2Fxxx&et=...&method=md5&sign=...）。
// 注意：token 具有有效期（et 字段），过期后需在控制台重新生成。
const char *mqtt_server = "mqtts.heclouds.com";
const uint16_t mqtt_port = 1883;
const char *product_id = "YOUR_PRODUCT_ID";
const char *device_id = "YOUR_DEVICE_ID";
const char *mqtt_auth_key = "YOUR_ONENET_AUTH_KEY";

WiFiClient espClient;
PubSubClient client(espClient);

// OneNET属性上报主题（与上面的 product_id / device_id 保持一致）
#define ONENET_TOPIC_PROP_POST "$sys/YOUR_PRODUCT_ID/YOUR_DEVICE_ID/thing/property/post"
#define ONENET_POST_BODY_FORMAT "{\"id\":\"%u\",\"params\":%s}"
uint16_t postMsgId = 0;
unsigned long lastMsgTime = 0;

// TFT屏幕硬件引脚
#define TFT_CS        10
#define TFT_RST       16
#define TFT_DC         1
#define TFT_MOSI      11
#define TFT_SCLK      12
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ESP32 <-> STM32 硬件串口定义
#define STM32_RX_PIN  18
#define STM32_TX_PIN  17
#define STM32_BAUD    115200
char stm32_rx_buf[128];
uint16_t rx_index = 0;

// 基础传感器全局变量
float ADCL = 0.0f;
float ADC_H = 0.0f;
float DHT11_H = 0.0f;
float DHT11_T = 0.0f;
bool new_sensor_data = false;

// 新增外设状态全局变量（适配一体化JSON）
uint8_t fan_state = 0;
uint8_t bulb_state = 0;
uint8_t pump_state = 0;
uint8_t servo_angle = 0;
uint8_t sys_mode = 0;  // 0=手动，1=自动
uint8_t ai_state = 0;

// WiFi连接函数
void setupWifi() {
  Serial.print("连接WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  uint8_t wifi_timeout = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_timeout < 20) {
    delay(500);
    Serial.print(".");
    wifi_timeout++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi连接成功 | IP:" + WiFi.localIP().toString());
  } else {
    ESP.restart();
  }
}

// TFT屏幕初始化
void initializeTFT() {
  tft.init(240, 280);
  tft.setRotation(3);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Smart Flower Pot");
  tft.setCursor(10, 35);
  tft.println("STM32 Data Collect");
  tft.drawLine(10, 85, 230, 85, ST77XX_WHITE);
}

// 解析STM32一体化复合JSON（温度+湿度+土壤+光照+所有外设状态）
void parseStm32Data(char *buf) {
  StaticJsonDocument<300> doc; // 扩容适配多字段JSON
  DeserializationError res = deserializeJson(doc, buf);
  if (res) return;

  // 环境传感器数据解析
  DHT11_T = doc["temp"].as<float>();
  DHT11_H = doc["humi"].as<float>();
  ADC_H = doc["soil"].as<int>();
  ADCL = doc["light"].as<float>();
  ai_state = doc["ai"].as<uint8_t>();

  // 外设状态解析
  fan_state = doc["fan"].as<uint8_t>();
  bulb_state = doc["bulb"].as<uint8_t>();
  pump_state = doc["pump"].as<uint8_t>();
  servo_angle = doc["servo"].as<uint8_t>();
  sys_mode = doc["mode"].as<uint8_t>(); // 0手动 1自动

  new_sensor_data = true;
}

// TFT局部刷新：环境数据 + 外设状态一体化显示
void refreshTFT() {
  tft.fillRect(10, 100, 220, 160, ST77XX_BLACK);

  tft.setTextColor(ST77XX_MAGENTA);
  tft.setTextSize(2);
  tft.setCursor(10, 105);
  tft.print("ADCL:"); tft.print(ADCL, 1); tft.print(" Lux");

  tft.setTextColor(ST77XX_BLUE);
  tft.setCursor(10, 135);
  tft.print("ADC_H:"); tft.print(ADC_H, 1); tft.print(" %RH");

  tft.setTextColor(ST77XX_GREEN);
  tft.setCursor(10, 165);
  tft.print("DHT_H:"); tft.print(DHT11_H, 1); tft.print(" %RH");

  tft.setTextColor(ST77XX_RED);
  tft.setCursor(10, 195);
  tft.print("DHT_T:"); tft.print(DHT11_T, 1); tft.print(" C");

  // 外设状态精简显示
  tft.fillRect(10, 215, 220, 14, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 215);
  tft.print("FAN:");tft.print(fan_state ? "ON " : "OFF ");
  tft.print("LIGHT:");tft.print(bulb_state ? "ON " : "OFF ");
  tft.print("MODE:");tft.print(sys_mode ? "AUTO" : "MANUAL");

  // MQTT在线状态显示
  tft.fillRect(10, 230, 220, 20, ST77XX_BLACK);
  tft.setTextColor(client.connected() ? ST77XX_GREEN : ST77XX_RED);
  tft.setTextSize(1);
  tft.setCursor(10, 230);
  tft.print("MQTT: ");
  tft.print(client.connected() ? "ONLINE" : "OFFLINE");
}

// OneNET云平台数据上报
void uploadToOneNET() {
  if (!client.connected()) return;
  char param_buf[256];
  char publish_buf[350];

  sprintf(param_buf,
    "{\"ADCL\":{\"value\":%.1f},\"ADC_H\":{\"value\":%.1f},\"DHT11_H\":{\"value\":%.1f},\"DHT11_T\":{\"value\":%.1f}}",
    ADCL, ADC_H, DHT11_H, DHT11_T);
  postMsgId++;
  sprintf(publish_buf, ONENET_POST_BODY_FORMAT, postMsgId, param_buf);
  client.publish(ONENET_TOPIC_PROP_POST, publish_buf);
}

// 安全非阻塞MQTT重连（杜绝死循环崩溃）
bool mqttReconnect() {
  Serial.println("尝试连接OneNET MQTT...");
  bool connect_ok = client.connect(device_id, product_id, mqtt_auth_key);
  if (connect_ok) {
    Serial.println("MQTT 连接成功！");
    tft.fillRect(10, 60, 220, 20, ST77XX_BLACK);
    tft.setCursor(10, 60);
    tft.setTextColor(ST77XX_GREEN);
    tft.print("MQTT CONNECTED");
    return true;
  } else {
    Serial.print("MQTT连接失败码: ");
    Serial.println(client.state());
    tft.fillRect(10, 60, 220, 20, ST77XX_BLACK);
    tft.setCursor(10, 60);
    tft.setTextColor(ST77XX_RED);
    tft.print("MQTT ERR:");
    tft.print(client.state());
    return false;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500); // 等待串口硬件稳定初始化
  Wire.begin(3, 13);
  initializeTFT();
  setupWifi();

  // 屏幕打印本机WiFi IP
  tft.fillRect(10, 60, 220, 20, ST77XX_BLACK);
  tft.setCursor(10, 60);
  tft.setTextColor(ST77XX_GREEN);
  tft.print("IP:");
  tft.println(WiFi.localIP());

  // 初始化ESP32与STM32硬件串口
  Serial1.begin(STM32_BAUD, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);
  Serial.println("ESP32 ↔ STM32 串口就绪");

  // MQTT服务器配置，延后连接避免网络栈未就绪
  client.setServer(mqtt_server, mqtt_port);
  client.setKeepAlive(120);
  delay(1000);
}

void loop() {
  // 1. 接收STM32一体化JSON数据，整行原样转发给Python
  while (Serial1.available() > 0) {
    char ch = Serial1.read();
    if (ch == '\n') {
      stm32_rx_buf[rx_index] = '\0';
      Serial.println(stm32_rx_buf); // 完整JSON透传PC，Python直接解析

      // 仅JSON数据执行解析
      if (stm32_rx_buf[0] == '{') {
        parseStm32Data(stm32_rx_buf);
      }
      // 清空缓冲区
      rx_index = 0;
      memset(stm32_rx_buf, 0, sizeof(stm32_rx_buf));
    } else if (ch != '\r' && rx_index < 126) {
      stm32_rx_buf[rx_index++] = ch;
    }
  }

  // 2. 接收Python网页控制指令，转发给STM32执行外设控制
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      Serial1.print(cmd);
      Serial1.write('\n');
    }
  }

  // 3. WiFi断线重连
  if (!WiFi.isConnected()) setupWifi();

  // 4. MQTT异步定时重试，非阻塞设计
  static unsigned long mqtt_retry_tick = 0;
  if (!client.connected()) {
    if (millis() - mqtt_retry_tick > 4000) {
      mqtt_retry_tick = millis();
      mqttReconnect();
    }
  }

  // 5. 仅收到新数据才刷新TFT屏幕
  if (new_sensor_data) {
    refreshTFT();
    new_sensor_data = false;
  }

  // 6. 5秒间隔稳定上报OneNET云平台
  if (millis() - lastMsgTime >= 5000 && client.connected()) {
    lastMsgTime = millis();
    uploadToOneNET();
  }

  client.loop();
  delay(100); // 降低主线程调度频率，缓解多外设总线竞争
}