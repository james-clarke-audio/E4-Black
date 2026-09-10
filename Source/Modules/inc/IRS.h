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


// ---------------------------------------------------------------------------
// Background (non-blocking) sensor sampler
//
// The blocking reads above spend ~235 us on a full four-detector set, and
// almost all of that is Irs_Delay busy-waiting for the emitter/detector to
// settle - only ~16 us is actual conversion. That is fine standing still, but
// unusable while driving, where the wall flags must stay live and the 1 kHz
// control loop must not be stalled.
//
// The sampler spreads the same two-phase dark/lit sequence over five 1 kHz
// ticks, one state per tick:
//
//   tick 0  DARK  all emitters off -> read the four ambient baselines,
//                 then switch the SL emitter on
//   tick 1  SL    read SL lit, subtract its baseline; SL off, FL on
//   tick 2  FL    read FL lit, subtract;              FL off, FR on
//   tick 3  FR    read FR lit, subtract;              FR off, SR on
//   tick 4  SR    read SR lit, subtract;              SR off, publish the set
//
// A complete ambient-subtracted set therefore lands every 5 ms (200 Hz) while
// the ISR does nothing but ADC conversions - no busy-wait at all. Because an
// emitter is left on for a whole tick the settle is ~1 ms, 20x
// IR_LIT_SETTLE_COUNT, so the detector is always fully settled and the settle
// count stops being a tuning knob on this path.
//
// The cost is emitter duty: 1 ms on in each 5 ms cycle is 20%, against ~1% for
// the blocking read - roughly 18 mA average at the ~90 mA pulse, well inside
// the emitter's 100 mA continuous rating, but it is a real change in average
// IR power and worth knowing when comparing readings between the two paths.
// ---------------------------------------------------------------------------
void     Irs_SM_Enable(uint8_t on);         // arm/disarm the background sampler
uint8_t  Irs_SM_Enabled(void);
void     Irs_SM_Reset(void);                // drop to the DARK state, emitters off
void     Irs_Tick(void);                    // call once per tick from the 1 kHz ISR
uint32_t Irs_Get_Latest(uint32_t out[5]);   // copy the latest published set;
                                            // returns its sequence number, 0 = none yet

// ADC ownership. Thread-context code that drives the ADC itself takes this
// lock so the sampler skips its tick rather than reconfiguring the converter
// underneath an in-flight conversion. Analog_Read takes it internally; the
// batched reads take it around the whole dark/lit sequence so the sampler
// cannot flip an emitter mid-batch. Nesting is counted.
//
// This matters because the low-battery guard reads the pack with the same
// blocking ADC from the main loop, and Analog_Read polls with HAL_MAX_DELAY:
// an ISR that stopped the converter under it would hang that read forever.
void Irs_Adc_Lock(void);
void Irs_Adc_Unlock(void);


#ifdef __cplusplus
}
#endif

#endif /* IRS_H_ */
