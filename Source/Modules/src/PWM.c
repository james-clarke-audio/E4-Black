/*
 * PWM.c
 *
 *  Created on: Nov 10, 2022
 *      Author: jamesclarke
 */

#include <config.h>
#include "PWM.h"

extern TIM_HandleTypeDef htim3;

#define PCLK			(HAL_RCC_GetPCLK1Freq())
#define PWMFREQ			(10000)	// Motor operating frequency [Hz]
#define MOT_DUTY_MIN	(30)		// minimum motor duty cycle
#define MOT_DUTY_MAX	(950)		// maximum duty of motor
// PWM full-scale = the timer's real auto-reload+1 (CubeMX set Period=479),
// NOT PCLK/PWMFREQ. duty[0..1000] maps linearly onto [0..MOT_FULLSCALE].
#define MOT_FULLSCALE	((uint32_t)(htim3.Init.Period + 1))

// Orient the motor
#define MOT_SET_COMPARE_L_FORWARD(x)	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, x)
#define MOT_SET_COMPARE_L_REVERSE(x)	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, x)
#define MOT_SET_COMPARE_R_FORWARD(x)	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, x)
#define MOT_SET_COMPARE_R_REVERSE(x)	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, x)

/* ---------------------------------------------------------------
	start timer for motor
--------------------------------------------------------------- */
void Motor_Initialize( void )
{
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
	HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4);
}

/* ---------------------------------------------------------------
	stops the rotation of the motors
--------------------------------------------------------------- */
void Motor_StopPWM( void )
{
	MOT_SET_COMPARE_L_FORWARD( 0xffff );
	MOT_SET_COMPARE_L_REVERSE( 0xffff );
	MOT_SET_COMPARE_R_FORWARD( 0xffff );
	MOT_SET_COMPARE_R_REVERSE( 0xffff );
}

/* ---------------------------------------------------------------
	Rotate the left motor with the specified duty [0-1000]
--------------------------------------------------------------- */
void Motor_SetDuty_Left( int16_t duty_l )
{
	uint32_t	pulse_l;

	if( ABS(duty_l) > MOT_DUTY_MAX )
	{
		pulse_l = (uint32_t)(MOT_FULLSCALE * MOT_DUTY_MAX / 1000) - 1;
	}
	else if( ABS(duty_l) < MOT_DUTY_MIN )
	{
		pulse_l = (uint32_t)(MOT_FULLSCALE * MOT_DUTY_MIN / 1000) - 1;
	}
	else
	{
		pulse_l = (uint32_t)(MOT_FULLSCALE * ABS(duty_l) / 1000) - 1;
	}

	if( duty_l > 0 )
	{
		MOT_SET_COMPARE_L_FORWARD( pulse_l );
		MOT_SET_COMPARE_L_REVERSE( 0 );
	}
	else if( duty_l < 0 )
	{
		MOT_SET_COMPARE_L_FORWARD( 0 );
		MOT_SET_COMPARE_L_REVERSE( pulse_l );
	}
	else
	{
		MOT_SET_COMPARE_L_FORWARD( 0 );
		MOT_SET_COMPARE_L_REVERSE( 0 );
	}
}

/* ---------------------------------------------------------------
	Rotate the right motor with the specified duty [0-1000]
--------------------------------------------------------------- */
void Motor_SetDuty_Right( int16_t duty_r )
{
	uint32_t	pulse_r;

	if( ABS(duty_r) > MOT_DUTY_MAX )
	{
		pulse_r = (uint32_t)(MOT_FULLSCALE * MOT_DUTY_MAX / 1000) - 1;
	}
	else if( ABS(duty_r) < MOT_DUTY_MIN )
	{
		pulse_r = (uint32_t)(MOT_FULLSCALE * MOT_DUTY_MIN / 1000) - 1;
	}
	else
	{
		pulse_r = (uint32_t)(MOT_FULLSCALE * ABS(duty_r) / 1000) - 1;
	}

	if( duty_r > 0 )
	{
		MOT_SET_COMPARE_R_FORWARD( pulse_r );
		MOT_SET_COMPARE_R_REVERSE( 0 );
	}
	else if( duty_r < 0 )
	{
		MOT_SET_COMPARE_R_FORWARD( 0 );
		MOT_SET_COMPARE_R_REVERSE( pulse_r );
	}
	else
	{
		MOT_SET_COMPARE_R_FORWARD( 0 );
		MOT_SET_COMPARE_R_REVERSE( 0 );
	}
}

