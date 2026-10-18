#include "dht11.h"
#include "tim_delay.h" 
// 微秒级延时函数声明（需提前实现，如基于TIM2的delay_us）
extern void delay_us(uint16_t us);

// DHT11初始化
// 返回0：初始化成功，1：失败
uint8_t DHT11_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 使能GPIOB时钟（PB5所在端口）
    __HAL_RCC_GPIOB_CLK_ENABLE();

    // 初始化为输出模式（推挽输出、上拉、高速）
    GPIO_InitStruct.Pin = DHT11_PIN;                  // PB5引脚
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;        // 推挽输出模式
    GPIO_InitStruct.Pull = GPIO_PULLUP;                // 上拉电阻
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;      // 高速模式（100MHz）
    HAL_GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStruct);  // 初始化GPIOB

    HAL_GPIO_WritePin(DHT11_GPIO_PORT, DHT11_PIN, GPIO_PIN_SET);  // 初始拉高总线

    DHT11_Rst();         // 发送复位信号
    return DHT11_Check();// 检测传感器响应
}

// 复位DHT11（发送起始信号）
void DHT11_Rst(void)
{
    DHT11_IO_OUT();                          // 切换PB5为输出模式
    DHT11_DQ_OUT(GPIO_PIN_RESET);            // 拉低总线
    HAL_Delay(20);                           // 拉低至少18ms（用HAL库毫秒延时）
    DHT11_DQ_OUT(GPIO_PIN_SET);              // 释放总线（拉高）
    delay_us(30);                            // 等待20~40us（用微秒延时）
}

// 等待DHT11的回应
// 返回1：未检测到DHT11；0：检测到DHT11
uint8_t DHT11_Check(void)
{
    uint8_t retry = 0;
    DHT11_IO_IN();  // 切换PB5为输入模式

    // 等待DHT11拉低总线（响应起始信号），超时100us
    while (DHT11_DQ_IN() == GPIO_PIN_SET && retry < 100)
    {
        retry++;
        delay_us(1);
    }
    if (retry >= 100) return 1;  // 无响应

    retry = 0;
    // 等待DHT11释放总线（拉高），超时100us
    while (DHT11_DQ_IN() == GPIO_PIN_RESET && retry < 100)
    {
        retry++;
        delay_us(1);
    }
    if (retry >= 100) return 1;  // 响应异常

    return 0;  // 响应正常
}

// 从DHT11读取1个位
// 返回值：0或1
uint8_t DHT11_Read_Bit(void)
{
    uint8_t retry = 0;

    // 等待总线拉低（数据位起始信号）
    while (DHT11_DQ_IN() == GPIO_PIN_SET && retry < 100)
    {
        retry++;
        delay_us(1);
    }
    retry = 0;

    // 等待总线拉高（数据位有效信号）
    while (DHT11_DQ_IN() == GPIO_PIN_RESET && retry < 100)
    {
        retry++;
        delay_us(1);
    }

    delay_us(40);  // 等待40us（超过0信号的26~28us，未到1信号的116~118us）

    // 读取当前电平：高电平为1，低电平为0
    if (DHT11_DQ_IN() == GPIO_PIN_SET)
        return 1;
    else
        return 0;
}

// 从DHT11读取1个字节（8位）
// 返回值：读取到的字节数据
uint8_t DHT11_Read_Byte(void)
{
    uint8_t i, dat = 0;
    for (i = 0; i < 8; i++)
    {
        dat <<= 1;               // 左移1位，准备接收新位
        dat |= DHT11_Read_Bit(); // 接收1位数据
    }
    return dat;
}

// 从DHT11读取温湿度数据
// temp：温度值（0~50℃）；humi：湿度值（20%~90%）
// 返回0：读取成功；1：读取失败
uint8_t DHT11_Read_Data(volatile float *temp, volatile float *humi)
{
    uint8_t buf[5];  // 存储5字节数据（湿度高8位、湿度低8位、温度高8位、温度低8位、校验位）
    uint8_t i;

    DHT11_Rst();               // 发送复位信号
    if (DHT11_Check() == 0)    // 传感器响应正常
    {
        for (i = 0; i < 5; i++)// 读取40位数据（5字节）
        {
            buf[i] = DHT11_Read_Byte();
        }

        // 校验数据（前4字节之和等于第5字节）
        if ((buf[0] + buf[1] + buf[2] + buf[3]) == buf[4])
        {
            *humi = buf[0];  // 湿度整数部分
            *temp = buf[2];  // 温度整数部分
        }
    }
    else
    {
        return 1;  // 读取失败
    }
    return 0;      // 读取成功
}

// 切换PB5为输出模式
void DHT11_IO_OUT(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_PIN;                  // PB5引脚
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;        // 推挽输出
    GPIO_InitStruct.Pull = GPIO_PULLUP;                // 上拉
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;      // 高速
    HAL_GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStruct);  // 初始化GPIOB
}

// 切换PB5为输入模式
void DHT11_IO_IN(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = DHT11_PIN;                  // PB5引脚
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;            // 输入模式
    GPIO_InitStruct.Pull = GPIO_PULLUP;                // 上拉（保持总线稳定）
    HAL_GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStruct);  // 初始化GPIOB
}
