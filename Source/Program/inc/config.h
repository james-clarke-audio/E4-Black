/*
 * index.h
 *
 *  Created on: 9 Nov 2022
 *      Author: jamesclarke
 */

#ifndef INC_CONFIG_H_
#define INC_CONFIG_H_

/* these are the system includes */
#include <stdio.h>
#include "main.h"
#include "stm32f4xx_hal.h"
#include "adc.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"
#include "systick.h"

//***************************************************************************//
// USER_MODE tells the software whether to run the built-in tests or custom
// user-provided routines. When set to false, execution is directed to the
// function run_tests() in tests.c. this is the default and it is where all
// the setup and configuration can be done.
// when you are ready to try your own code, change the values of USER_MODE to be
// true and execution will be directed to the function run_mouse() in user.c.

extern const uint8_t USER_MODE;

//***************************************************************************//
// We need to know about the drive mechanics.

extern const float WHEEL_DIAMETER; //33.298; // Adjust on test
extern const float ENCODER_PULSES;	// 512 each side
extern const float GEAR_RATIO;       // outer gear teeth / pinion gear teeth
extern const float MM_PER_COUNT_LEFT;    // mm per encoder count (500mm push-test)
extern const float MM_PER_COUNT_RIGHT;

// Mouse radius is the distance between the contact patches of the drive wheels.
// A good starting approximation is half the distance between the wheel centres.
// After testing, you may find the working value to be larger or smaller by some
// small amount.
extern const float MOUSE_RADIUS; //39.50; // Adjust on test
extern const float POSE_START_X;    // start pose of the axle/centre-of-rotation, mm
extern const float POSE_START_Y;
extern const float POSE_TEST_X_OFFSET;  // bench-only offset added to start X (0 for real runs)
extern const float POSE_TEST_Y_OFFSET;  // bench-only offset added to start Y (0 for real runs)

// The robot is likely to have wheels of different diameters and that must be
// compensated for if the robot is to reliably drive in a straight line
extern const float ROTATION_BIAS; // Negative makes robot curve to left

//*** MOTION CONTROL CONSTANTS **********************************************//

// forward motion controller constants
extern const float FWD_KP;
extern const float FWD_KD;

// rotation motion controller constants
extern const float ROT_KP;
extern const float ROT_KD;

// controller constants for the steering controller
extern const float STEERING_KP;
extern const float STEERING_KD;
extern const float STEERING_ADJUST_LIMIT; // deg/s

// Motor Feedforward
/***
 * Speed Feedforward is used to add a drive voltage proportional to the motor speed
 * The units are Volts per mm/s and the value will be different for each
 * robot where the motor + gearbox + wheel diamter + robot weight are different
 * You can experimentally determine a suitable value by turning off the controller
 * and then commanding a set voltage to the motors. The same voltage is applied to
 * each motor. Have the robot report its speed regularly or have it measure
 * its steady state speed after a period of acceleration.
 * Do this for several applied voltages from 0.5Volts to 3 Volts in steps of 0.5V
 * Plot a chart of steady state speed against voltage. The slope of that graph is
 * the speed feedforward, SPEED_FF.
 * Note that the line will not pass through the origin because there will be
 * some minimum voltage needed just to ovecome friction and get the wheels to turn at all.
 * That minimum voltage is the BIAS_FF. It is not dependent upon speed but is expressed
 * here as a fraction for comparison.
 */
extern const float SPEED_FF;
extern const float BIAS_FF;

// encoder polarity is set to account for reversal of the encoder phases
extern const int ENCODER_LEFT_POLARITY;
extern const int ENCODER_RIGHT_POLARITY;

// similarly, the motors may be wired with different polarity and that
// is defined here so that setting a positive voltage always moves the robot
// forwards
extern const int MOTOR_LEFT_POLARITY;
extern const int MOTOR_RIGHT_POLARITY;
extern const int GYRO_POLARITY;   // yaw-gyro sign for CCW-positive (verify on bench)
extern const float GYRO_SCALE;    // yaw-gyro scale trim (calibrate)

//*** CONTROL LOOP TIMING **************************************************//
// The main control loop runs at 1 kHz. Rate-dependent maths across the
// ported mazerunner logic references these two constants.
extern const float LOOP_FREQUENCY;   // Hz
extern const float LOOP_INTERVAL;    // seconds ( = 1 / LOOP_FREQUENCY )

/* Useful Constants */
#define G					(9.80665f)					// gravitational acceleration g (m/s^2)
#define PI					(3.1415926f)				// pi
#define SQRT2				(1.41421356237f)			// square root 2
#define SQRT3				(1.73205080757f)			// square root 3
#define SQRT5				(2.2360679775f)				// square root 5
#define SQRT7				(2.64575131106f)			// square root 7

/* macro functions */
#define DEG2RAD(x)			(((x)/180.0f)*PI)			// convert from degrees to radians
#define RAD2DEG(x)			(180.0f*((x)/PI))			// convert from radians to degrees
#define SWAP(a, b) 			((a != b) && (a += b, b = a - b, a -= b))
#define ABS(x) 				((x) < 0 ? -(x) : (x))		// Absolute value
#define SIGN(x)				((x) < 0 ? -1 : 1)			// sign
#define MAX(a, b) 			((a) > (b) ? (a) : (b))		// returns the larger of the two variables
#define MIN(a, b) 			((a) < (b) ? (a) : (b))		// returns the smaller of the two varibles
#define MAX3(a, b, c) 		((a) > (MAX(b, c)) ? (a) : (MAX(b, c)))
#define MIN3(a, b, c) 		((a) < (MIN(b, c)) ? (a) : (MIN(b, c)))

/* Macros used by mouse */
/* LEDS */

#define LED_LEFT_ON()			HAL_GPIO_WritePin(LED_LEFT_GPIO_Port, LED_LEFT_Pin, GPIO_PIN_SET)
#define LED_LEFT_OFF()			HAL_GPIO_WritePin(LED_LEFT_GPIO_Port, LED_LEFT_Pin, GPIO_PIN_RESET)
#define LED_LEFT_TOGGLE()		HAL_GPIO_TogglePin(LED_LEFT_GPIO_Port, LED_LEFT_Pin)
#define LED_RIGHT_ON()			HAL_GPIO_WritePin(LED_RIGHT_GPIO_Port, LED_RIGHT_Pin, GPIO_PIN_SET)
#define LED_RIGHT_OFF()			HAL_GPIO_WritePin(LED_RIGHT_GPIO_Port, LED_RIGHT_Pin, GPIO_PIN_RESET)
#define LED_RIGHT_TOGGLE()		HAL_GPIO_TogglePin(LED_RIGHT_GPIO_Port, LED_RIGHT_Pin)
#define LED_BLUE_ON()			HAL_GPIO_WritePin(LED_ON_GPIO_Port, LED_ON_Pin, GPIO_PIN_SET)
#define LED_BLUE_OFF()			HAL_GPIO_WritePin(LED_ON_GPIO_Port, LED_ON_Pin, GPIO_PIN_RESET)
#define LED_BLUE_TOGGLE()		HAL_GPIO_TogglePin(LED_ON_GPIO_Port, LED_ON_Pin)

#define LED_ALL_ON()			HAL_GPIO_WritePin (GPIOB, LED_LEFT_Pin|LED_RIGHT_Pin, GPIO_PIN_SET)
#define LED_ALL_OFF()			HAL_GPIO_WritePin (GPIOB, LED_LEFT_Pin|LED_RIGHT_Pin, GPIO_PIN_RESET)
#define LED_ALL_TOGGLE()		HAL_GPIO_TogglePin(GPIOB, LED_LEFT_Pin|LED_RIGHT_Pin)

/* SWITCHES */
#define SWITCH_LEFT()			HAL_GPIO_ReadPin(GPIOA, BUTTON_LEFT_Pin)
#define SWITCH_RIGHT()			HAL_GPIO_ReadPin(GPIOA, BUTTON_RIGHT_Pin)

/* Interrupt-atomic block guard used by the ported mazerunner logic.
 * On the AVR this disabled interrupts around multi-field updates. On the
 * Cortex-M4, aligned 32-bit/float accesses are already atomic w.r.t.
 * interrupts, so this is a no-op for now. TODO: wrap in __disable_irq()/
 * __enable_irq() if a profile's multi-field update ever needs protecting
 * once profiles run inside the systick ISR. */
#ifndef ATOMIC
#define ATOMIC
#endif





#endif /* INC_CONFIG_H_ */
