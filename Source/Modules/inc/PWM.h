/*
 * PWM.h
 *
 *  Created on: Nov 10, 2022
 *      Author: jamesclarke
 *
 *
 *
 *
 * TIM3 is used to generate both PWM signals (left and right motor):
 *
 * - Edge-aligned, up-counting timer.
 * - Prescale to increment timer counter at 24 MHz.
 * - Set PWM frequency to 24 kHz.
 * - Configure channels 1, 2, 3 and 4 as output GPIOs.
 * - Set output compare mode to PWM1 (output is active when the counter is
 *   less than the compare register contents and inactive otherwise.
 * - Reset output compare value (set it to 0).
 * - Enable channels 1, 2, 3 and 4 outputs.
 * - Enable counter for TIM3.
 *
*/

#ifndef INC_PWM_H_
#define INC_PWM_H_

#ifdef __cplusplus
extern "C" {
#endif


#include <stdio.h>
#include "main.h"
#include "stm32f4xx_hal.h"
#include "tim.h"
#include "gpio.h"

void Motor_Initialize( void );				// start timer for motor
void Motor_StopPWM( void );					// stops the rotation of the motors
void Motor_SetDuty_Left( int16_t );			// Rotate the left motor with the specified duty [0-1000]
void Motor_SetDuty_Right( int16_t );		// Rotate the right motor with the specified duty [0-1000]


#ifdef __cplusplus
}
#endif

#endif /* INC_PWM_H_ */
