/*
 * systick.h
 *
 *  Created on: Nov 7, 2022
 *      Author: jamesclarke
 */

#ifndef SYSTICK_H_
#define SYSTICK_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "main.h"

void SysTick_Function(void);
uint32_t get_systick_counter();


#ifdef __cplusplus
}
#endif

#endif /* SYSTICK_H_ */
