/*
 * encoders.c
 *
 *  Created on: Nov 10, 2022
 *      Author: jamesclarke
 */

#include <config.h>
#include "encoders.h"

#define ENC_CNT_L (TIM2 -> CNT)
#define ENC_CNT_R (TIM4 -> CNT)

#define ENC_ZERO (20000)
#define ENC_RESOLUTION (1024 - 1)

void Encoder_Initialize(void)
{
	HAL_TIM_Encoder_Start( &htim2, TIM_CHANNEL_ALL );
	HAL_TIM_Encoder_Start( &htim4, TIM_CHANNEL_ALL );
}

uint16_t Encoder_Read_Left(void)
{
    return (uint16_t)ENC_CNT_L;
	//return (uint16_t)timer_get_counter(TIM2);
}

//uint32_t timer_get_counter(uint32_t timer_peripheral)
//{
	//return TIMER_TC(timer_peripheral);
//}


uint16_t Encoder_Read_Right(void)
{
	return (uint16_t)ENC_CNT_R;
	//return (uint16_t)timer_get_counter(TIM4);
}

void Encoder_ResetCount_Left(void)
{
	ENC_CNT_L = ENC_ZERO;
}

void Encoder_ResetCount_Right(void)
{
	ENC_CNT_R = ENC_ZERO;
}

float Encoder_GetAngle_Left(void)
{
	return(2 * PI * (float)( (int32_t)ENC_CNT_L - (int32_t)ENC_ZERO ) / (float)ENC_RESOLUTION);
}

float Encoder_GetAngle_Right(void)
{
	return(2 * PI * (float)( (int32_t)ENC_ZERO - (int32_t)ENC_CNT_R ) / (float)ENC_RESOLUTION);
}
