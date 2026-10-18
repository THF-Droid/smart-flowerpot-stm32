#ifndef _DHT11_H
#define _DHT11_H

#include "stm32f4xx_hal.h"  // HAL库核心头文件

// 引脚定义（修改为PB5）
#define DHT11_PIN         GPIO_PIN_5   // 引脚：PB5
#define DHT11_GPIO_PORT   GPIOB        // 端口：GPIOB

// 输入输出操作宏（HAL库风格，针对PB5）
#define DHT11_DQ_IN()     HAL_GPIO_ReadPin(DHT11_GPIO_PORT, DHT11_PIN)  // 读取PB5状态
#define DHT11_DQ_OUT(state)  HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_PIN, (GPIO_PinState)state)  // 输出状态到PB5（state为GPIO_PIN_SET/GPIO_PIN_RESET）

// 函数声明（接口不变，操作对象改为PB5）
void DHT11_IO_OUT(void);                  // 切换PB5为输出模式
void DHT11_IO_IN(void);                   // 切换PB5为输入模式
uint8_t DHT11_Init(void);                 // 初始化DHT11（基于PB5）
void DHT11_Rst(void);                     // 通过PB5发送复位信号
uint8_t DHT11_Check(void);                // 通过PB5检测传感器响应
uint8_t DHT11_Read_Bit(void);             // 通过PB5读取1位数据
uint8_t DHT11_Read_Byte(void);            // 通过PB5读取1字节数据
uint8_t DHT11_Read_Data(volatile float *temp, volatile float *humi);  // 读取温湿度数据（依赖PB5）

#endif
