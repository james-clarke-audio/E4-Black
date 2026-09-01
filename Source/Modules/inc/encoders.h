/*
 * encoders.h
 *
 *  Created on: Nov 10, 2022
 *      Author: jamesclarke
 */

#ifndef INC_ENCODERS_H_
#define INC_ENCODERS_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "main.h"
//#include "stm32f4xx_hal.h"
#include "tim.h"
//#include "gpio.h"

void Encoder_Initialize(void);
uint16_t Encoder_Read_Left(void);
uint16_t Encoder_Read_Right(void);
void Encoder_ResetCount_Left(void);
void Encoder_ResetCount_Right(void);
float Encoder_GetAngle_Left(void);
float Encoder_GetAngle_Right(void);


#ifdef __cplusplus
}
#endif

#endif /* INC_ENCODERS_H_ */
