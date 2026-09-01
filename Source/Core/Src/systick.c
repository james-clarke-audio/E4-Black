/*
 * systick.c
 *
 *  Created on: Nov 7, 2022
 *      Author: jamesclarke
 */

#include "systick.h"
//#include "pid.h"


uint32_t systick_counter = 0;

uint32_t get_systick_counter() {
	// Return current value of systick_counter variable
	return systick_counter;
}

/* Will be called every millisecond */
void SysTick_Function(void) {

	/* Do whatever you wish to be done every millisecond. Maybe... update your PID? */
	//PID_Update();

	// Just increase the systick variable
	systick_counter++;

	//HAL_GPIO_TogglePin(SYS_FLAG_GPIO_Port, SYS_FLAG_Pin); // Toggle Output

}

