/*
 * IRS.h
 *
 *  Created on: Nov 8, 2022
 *      Author: jamesclarke
 */

#ifndef IRS_H_
#define IRS_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "main.h"
#include "adc.h"

#define NUM_SAMPLES (uint8_t) 1 //you may want to add a number of samples to read each IR/average these readings, remember the time delay on the IRs is mostly turning them on and off, so leaving them on and measuring a few times isn't bad
#define DELAY_COUNT 300 //emitter settle before the lit sample (tunable via the IR monitor)

typedef enum
{
  BATTERY = 0,
  IR_SIDE_RIGHT = 1,
  IR_FRONT_RIGHT = 2,
  IR_FRONT_LEFT = 3,
  IR_SIDE_LEFT = 4,
} ADCSensors;

float Battery_GetVoltage(void);

uint32_t Irs_Read_Battery(void);
uint32_t Irs_Read_SideRight(void);
uint32_t Irs_Read_FrontRight(void);
uint32_t Irs_Read_FrontLeft(void);
uint32_t Irs_Read_SideLeft(void);
uint32_t Irs_Read(ADCSensors ir);
uint32_t Irs_Read_Diff(ADCSensors ir);   // ambient-subtracted (dark->on->lit)
uint32_t Analog_Read(ADCSensors ir);


#ifdef __cplusplus
}
#endif

#endif /* IRS_H_ */
