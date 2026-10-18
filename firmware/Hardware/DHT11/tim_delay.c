#include "tim_delay.h"

extern TIM_HandleTypeDef htim2;  // 定义TIM2句柄

// 初始化TIM2为1MHz计数（1计数=1微秒）
void TIM2_Init(void)
{
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    // 使能TIM2时钟（APB1总线）
    __HAL_RCC_TIM2_CLK_ENABLE();

    // 配置TIM2参数
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 99;  // 预分频：100MHz/(99+1)=1MHz（1us/计数）
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;  // 向上计数
    htim2.Init.Period = 0xFFFF;  // 最大计数（65535us，足够覆盖DHT11所需的微秒延时）
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
    {
        Error_Handler();  // 初始化失败处理（需在工程中实现Error_Handler）
    }

    // 主模式配置（无需触发其他外设）
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }
}

// 微秒延时函数（输入范围：1~65535 us）
void delay_us(uint16_t us)
{
    __HAL_TIM_DISABLE(&htim2);      // 停止定时器
    TIM2->CNT = 0;                  // 清空计数器
    __HAL_TIM_ENABLE(&htim2);       // 启动定时器

    while (TIM2->CNT < us);         // 等待计数达到目标值

    __HAL_TIM_DISABLE(&htim2);      // 停止定时器
}
