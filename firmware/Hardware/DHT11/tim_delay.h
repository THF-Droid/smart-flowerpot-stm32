#ifndef __TIM_DELAY_H
#define __TIM_DELAY_H

#include "stm32f4xx_hal.h"
extern TIM_HandleTypeDef htim2;  // 声明TIM2句柄




void TIM2_Init(void);            // 初始化TIM2（用于微秒延时）
void delay_us(uint16_t us);      // 微秒延时函数
void Error_Handler(void);  // 声明错误处理函数
#endif
