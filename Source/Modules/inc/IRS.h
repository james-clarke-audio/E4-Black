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

// Emitter/detector settle counts, in busy-loop iterations (each ~16 ns at 96 MHz, O0).
// LIT: emitter-on settle before the lit sample. The OP505C phototransistor rises
//   slowly on weak returns (the far forward wall), so this is the main range lever.
//   ~800 iterations ~= 50 us.
// DARK: settle after all emitters are switched off, before the ambient baseline is
//   read, so no detector is still bleeding charge from a pulse. ~300 ~= 18 us.
#define IR_LIT_SETTLE_COUNT  800   // ~50 us  (emitter on -> lit sample)
#define IR_DARK_SETTLE_COUNT 300   // ~18 us  (all emitters off -> dark baseline)

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
void Irs_Read_Diff_All(uint32_t out[5]);  // batched: DARK phase then per-emitter LIT phase
uint32_t Analog_Read(ADCSensors ir);
void Irs_Delay(uint32_t count);            // busy-loop settle (see IR_*_SETTLE_COUNT)
void Irs_Emitter_Set(ADCSensors ir, uint8_t on);   // diagnostics: hold an emitter on


#ifdef __cplusplus
}
#endif

#endif /* IRS_H_ */
