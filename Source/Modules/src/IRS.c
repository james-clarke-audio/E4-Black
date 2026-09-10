/*
 * IRS.c
 *
 *  Created on: Nov 8, 2022
 *      Author: jamesclarke
 */

#include "IRS.h"
#include "main.h"
#include "gpio.h"
#include "adc.h"

extern ADC_HandleTypeDef hadc1;

// --- ADC ownership -------------------------------------------------------
// One converter, two callers: thread context (battery guard, blocking wall
// reads, diagnostics) and the 1 kHz sampler tick. They must never overlap,
// because Analog_Read polls with HAL_MAX_DELAY - an ISR that reconfigured or
// stopped the ADC under a thread-context read would hang the main loop for
// good, and that loop is what feeds the low-battery cutoff.
//
// The rule is simple: thread context always wins. It raises this counter and
// the sampler skips its tick, retrying on the next one. That costs at most a
// handful of the 1000 ticks each second, so the 200 Hz sample rate is
// unaffected in practice.
//
// No critical section is needed. A thread only executes while no ISR is
// running, and the ISR always runs its ADC work to completion, so the two can
// never be interleaved: a plain counter is enough. Only thread context writes
// it; the ISR only reads it.
static volatile uint8_t s_adc_lock = 0;

// Raw single conversion, no locking. The body of Analog_Read; used directly by
// the sampler tick (which has already tested the lock) and wrapped for
// everyone else.
static uint32_t adc_read_channel(ADCSensors ir);

// Park the background sampler: emitters dark, sequence rewound to its first
// state. Defined with the sampler at the foot of this file.
static void irs_sm_park(void);

// Taking the lock does more than block the sampler's ADC access: it also puts
// the sampler's EMITTERS out of the way. The tick that was interrupted may
// have left one lit for its settle, and a blocking read that ran with a
// neighbour's emitter on would measure that neighbour's beam rather than an
// ambient baseline. Parking on the 0 -> 1 transition costs the sampler one
// partial cycle (it restarts cleanly from DARK once the lock clears) and
// guarantees the blocking path sees exactly the dark field it has always
// assumed.
void Irs_Adc_Lock(void)
{
	uint8_t outermost = (s_adc_lock == 0);
	++s_adc_lock;                 // stop the sampler taking a tick first...
	if (outermost) irs_sm_park(); // ...then clear whatever it left lit
}

void Irs_Adc_Unlock(void) { if (s_adc_lock) --s_adc_lock; }

#pragma GCC push_options
#pragma GCC optimize ("O0")
void Irs_Delay(uint32_t count) { //busy-loop settle; callers pass IR_LIT_SETTLE_COUNT / IR_DARK_SETTLE_COUNT
	volatile uint32_t counter = count;
	while(counter--);
}
#pragma GCC pop_options

//***************************************************************************//
// Battery resistor bridge //Derek Hall//
// The battery measurement is performed by first reducing the battery voltage
// with a potential divider formed by two resistors. Here they are named R1 and R2
// though that may not be their designation on the schematics.
//
// Resistor R1 is the high-side resistor and connects to the battery supply
// Resistor R2 is the low-side resistor and connects to ground
// Battery voltage is measured at the junction of these resistors
// The ADC port used for the conversion will have a full scale reading (FSR) that
// depends on the device being used. Typically that will be 1023 for a 10-bit ADC as
// found on an Arduino but it may be 4095 if you have a 12-bit ADC.
// Finally, the ADC converter on your processor will have a reference voltage. On
// the Arduinos for example, this is 5 Volts. Thus, a full scale reading of
// 1023 would represent 5 Volts, 511 would be 2.5Volts and so on.
//
// in this section you can enter the appropriate values for your ADC and potential
// divider setup to ensure that the battery voltage reading performed by the sensors
// is as accurate as possible.
//
// By calculating the battery multiplier here, you can be sure that the actual
// battery voltage calulation is done as efficiently as possible.
// The compiler will do all these calculations so your program does not have to.

const float BATTERY_R1 = 10000.0; //resistor to battery +
const float BATTERY_R2 = 10000.0; //resistor to Gnd
const float BATTERY_DIVIDER_RATIO = BATTERY_R2 / (BATTERY_R1 + BATTERY_R2);
const float ADC_FSR = 4095.0;    //The maximum reading for the ADC
const float ADC_REF_VOLTS = 3.3; //Reference voltage of ADC
// One-point calibration trim: multimeter 3.758 V (loaded) vs read 3.71 V -> x1.013.
const float BATTERY_CAL = 1.013f;
const float BATTERY_MULTIPLIER = (ADC_REF_VOLTS / ADC_FSR / BATTERY_DIVIDER_RATIO) * BATTERY_CAL;

float Battery_GetVoltage(void)
{
	return BATTERY_MULTIPLIER * (float)Irs_Read_Battery();
}

uint32_t Irs_Read_Battery(void) //these functions should use IRs_Read to read the right IR (just one line)
{
	return Irs_Read(BATTERY);
}

uint32_t Irs_Read_SideRight(void) //these functions should use IRs_Read to read the right IR (just one line)
{
	return Irs_Read(IR_SIDE_RIGHT);
}


uint32_t Irs_Read_FrontRight(void)
{
	return Irs_Read(IR_FRONT_RIGHT);
}


uint32_t Irs_Read_FrontLeft(void)
{
	return Irs_Read(IR_FRONT_LEFT);
}

uint32_t Irs_Read_SideLeft(void)
{
	return Irs_Read(IR_SIDE_LEFT);
}

// this function should handle turning on an IR emitter with the correct port and pin (depending on which IR is passed to it),
// it should then call Analog_Read to read the IR value from the right IR, and finally turn off the IR emitter.
// don't forget to delay between turning on, reading, and turning off your IRs!
uint32_t Irs_Read(ADCSensors ir)
{

	GPIO_TypeDef* port;
	uint16_t pin;
	uint32_t value;

	switch(ir) {
		case IR_SIDE_RIGHT:
			port = IR_SIDE_RIGHT_GPIO_Port;
			pin = IR_SIDE_RIGHT_Pin;
			break;

		case IR_FRONT_RIGHT:
			port = IR_FRONT_RIGHT_GPIO_Port;
			pin = IR_FRONT_RIGHT_Pin;
			break;

		case IR_FRONT_LEFT:
			port = IR_FRONT_LEFT_GPIO_Port;
			pin = IR_FRONT_LEFT_Pin;
			break;

		case IR_SIDE_LEFT:
			port = IR_SIDE_LEFT_GPIO_Port;
			pin = IR_SIDE_LEFT_Pin;
			break;
		default:
			break;
	}

	switch(ir) {
		case BATTERY:
			// get battery reading
			value = Analog_Read(ir);

			break;
		default:
			Irs_Adc_Lock();   // own the emitters + ADC for the whole pulse
			// Turn on LED
			HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
			// Wait a bit...
			Irs_Delay(IR_LIT_SETTLE_COUNT);
			// Get reading and turn of LED
			value = Analog_Read(ir);
			HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
			// Wait again...
			Irs_Delay(IR_DARK_SETTLE_COUNT);
			Irs_Adc_Unlock();
			break;

	}

	return value;
}

// Map a sensor to its emitter GPIO port+pin. Returns 0 for BATTERY (no emitter).
static int ir_port_pin(ADCSensors ir, GPIO_TypeDef **port, uint16_t *pin)
{
	switch (ir) {
		case IR_SIDE_RIGHT:  *port = IR_SIDE_RIGHT_GPIO_Port;  *pin = IR_SIDE_RIGHT_Pin;  return 1;
		case IR_FRONT_RIGHT: *port = IR_FRONT_RIGHT_GPIO_Port; *pin = IR_FRONT_RIGHT_Pin; return 1;
		case IR_FRONT_LEFT:  *port = IR_FRONT_LEFT_GPIO_Port;  *pin = IR_FRONT_LEFT_Pin;  return 1;
		case IR_SIDE_LEFT:   *port = IR_SIDE_LEFT_GPIO_Port;   *pin = IR_SIDE_LEFT_Pin;   return 1;
		default: return 0;
	}
}

// Drive one emitter's GPIO directly (diagnostics: camera-based aiming). No ADC.
void Irs_Emitter_Set(ADCSensors ir, uint8_t on)
{
	GPIO_TypeDef *port; uint16_t pin;
	if (!ir_port_pin(ir, &port, &pin)) return;
	HAL_GPIO_WritePin(port, pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

// Ambient-subtracted read: sample dark (emitter off), pulse the emitter, sample
// lit, subtract. Rejects room/IR ambient so wall thresholds are stable. Clamped
// at 0. This is what the wall-sensing layer should use (not the raw Irs_Read).
uint32_t Irs_Read_Diff(ADCSensors ir)
{
	GPIO_TypeDef *port; uint16_t pin;
	if (!ir_port_pin(ir, &port, &pin)) return Analog_Read(ir);   // battery: raw
	// Hold the ADC for the whole dark/lit pair: the sampler must not flip an
	// emitter or reconfigure the converter between the two samples.
	Irs_Adc_Lock();
	uint32_t dark = Analog_Read(ir);            // ambient (emitter off)
	HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET); // emitter on
	Irs_Delay(IR_LIT_SETTLE_COUNT);
	uint32_t lit = Analog_Read(ir);             // ambient + reflected
	HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
	Irs_Delay(IR_DARK_SETTLE_COUNT);
	Irs_Adc_Unlock();
	return (lit > dark) ? (lit - dark) : 0;
}

// Batched ambient-subtracted read of the four IR detectors, in two clean phases:
//   1. DARK: every emitter off, settle (IR_DARK_SETTLE_COUNT), then read each
//            detector's ambient baseline back-to-back.
//   2. LIT:  one emitter at a time - on, settle (IR_LIT_SETTLE_COUNT), read that
//            detector, off - so a sensor never sees a neighbour's beam.
// Because every dark sample is taken with all emitters off (and after a settle),
// no detector is still bleeding charge from a pulse when its baseline is measured,
// giving a cleaner, larger diff than the old per-sensor interleaved read.
// out[] is indexed by ADCSensors; entries IR_SIDE_RIGHT..IR_SIDE_LEFT are filled.
void Irs_Read_Diff_All(uint32_t out[5])
{
	static const ADCSensors order[4] = {
		IR_SIDE_LEFT, IR_FRONT_LEFT, IR_FRONT_RIGHT, IR_SIDE_RIGHT
	};
	uint32_t dark[5] = {0};

	// Hold the ADC across both phases (see Irs_Adc_Lock): a sampler tick
	// landing mid-sequence would switch emitters under us.
	Irs_Adc_Lock();

	// Phase 1: DARK. All emitters off, let the detectors settle, read all ambients.
	for (int i = 0; i < 4; ++i) Irs_Emitter_Set(order[i], 0);
	Irs_Delay(IR_DARK_SETTLE_COUNT);
	for (int i = 0; i < 4; ++i) dark[order[i]] = Analog_Read(order[i]);

	// Phase 2: LIT. One emitter at a time; subtract this sensor's phase-1 baseline.
	for (int i = 0; i < 4; ++i) {
		ADCSensors s = order[i];
		Irs_Emitter_Set(s, 1);
		Irs_Delay(IR_LIT_SETTLE_COUNT);
		uint32_t lit = Analog_Read(s);
		Irs_Emitter_Set(s, 0);
		out[s] = (lit > dark[s]) ? (lit - dark[s]) : 0;
	}

	Irs_Adc_Unlock();
}

// Public single-channel read: takes the ADC lock so the background sampler
// stands aside for the duration.
uint32_t Analog_Read(ADCSensors ir)
{
	Irs_Adc_Lock();
	uint32_t v = adc_read_channel(ir);
	Irs_Adc_Unlock();
	return v;
}

static uint32_t adc_read_channel(ADCSensors ir)
{
	uint32_t channel;

	switch(ir)
	{
		case BATTERY:
			channel = ADC_CHANNEL_0;
			break;
		case IR_SIDE_RIGHT: //this picks the IR direction to choose the right ADC.
			channel = ADC_CHANNEL_2;
			break;
		case IR_FRONT_RIGHT:
			channel = ADC_CHANNEL_3;
			break;
		case IR_FRONT_LEFT:
			channel = ADC_CHANNEL_4;
			break;
		case IR_SIDE_LEFT:
			channel = ADC_CHANNEL_5;
			break;
		default:
			return 0;
	}

	ADC_ChannelConfTypeDef sConfig = {0}; //this initializes the IR ADC [Analog to Digital Converter]
	ADC_HandleTypeDef *hadc1_ptr = &hadc1;                  //this is a pointer to your hal_adc, you will need this when you call HAL_ADC_PollForConversion
	//this pointer will also be used to read the analog value, val = HAL_ADC_GetValue(hadc1_ptr);

	sConfig.Channel = channel;
	sConfig.Rank = 1;
	// Match the ADC sample window to the source impedance so the S&H cap charges
	// fully. Battery divider ~5k source -> 480 cycles. IR detectors sit on a 1.8k
	// load to ground (~1.8k source, worst case near cutoff): a 1.8k source needs
	// ~300 ns to settle at 12-bit, but 3 cycles is only ~125 ns, so the old setting
	// read the (weak, forward) returns systematically low. 28 cycles (~1.17 us) gives
	// ~4x margin at a negligible ~1 us/conversion cost.
	sConfig.SamplingTime = (ir == BATTERY) ? ADC_SAMPLETIME_480CYCLES : ADC_SAMPLETIME_28CYCLES;
	HAL_ADC_ConfigChannel(hadc1_ptr, &sConfig);

	HAL_ADC_Start(hadc1_ptr); //this starts the ADC

	uint32_t sum = 0;
	uint8_t measurements = 0;

	// NOTE: this poll is unbounded (HAL_MAX_DELAY). It is reachable from the
	// 1 kHz sampler tick, so a converter that never raised EOC would hang the
	// ISR rather than just the main loop. HAL_MAX_DELAY at least skips HAL's
	// HAL_GetTick() timeout path, which would deadlock inside SysTick anyway.
	// Left as-is deliberately: this read is the proven one, and bounding it is
	// a change to make on the bench, not alongside a new state machine.
	while(measurements < NUM_SAMPLES) //this takes multiple measurements
	{
		if(HAL_ADC_PollForConversion(hadc1_ptr,HAL_MAX_DELAY) == HAL_OK) //this makes sure the ADC has recieved a value
		{
			sum += HAL_ADC_GetValue(hadc1_ptr); // this is actually doing the reading
			++measurements;
		}
	}

	HAL_ADC_Stop(hadc1_ptr); //this stops the ADC
	return sum/NUM_SAMPLES;
}


//***************************************************************************//
// Background sensor sampler -- the non-blocking read (see IRS.h for the model)
//
// One state per 1 kHz tick, five ticks to a full ambient-subtracted set:
//
//   S_DARK -> read four ambients (all emitters off), light SL
//   S_SL   -> read SL lit, diff, SL off, light FL
//   S_FL   -> read FL lit, diff, FL off, light FR
//   S_FR   -> read FR lit, diff, FR off, light SR
//   S_SR   -> read SR lit, diff, SR off, publish
//
// Emitters are switched at the END of a tick and sampled at the START of the
// next, so every lit sample sits ~1 ms after its emitter came on -- 20x the
// old IR_LIT_SETTLE_COUNT busy-wait, for free, because the wait is the tick
// interval itself rather than CPU time.
//
// The only work in the ISR is ADC conversions: four in the DARK tick (~16 us
// worst case) and one in each lit tick (~4 us). No Irs_Delay, no HAL_Delay.
//***************************************************************************//

// Lit order. Matches Irs_Read_Diff_All so both paths give the same numbers.
static const ADCSensors SM_ORDER[4] = {
	IR_SIDE_LEFT, IR_FRONT_LEFT, IR_FRONT_RIGHT, IR_SIDE_RIGHT
};

#define SM_S_DARK 0            // states 1..4 are lit reads of SM_ORDER[state-1]

static volatile uint8_t  s_sm_on    = 0;   // armed?
static volatile uint8_t  s_sm_state = SM_S_DARK;
static uint32_t          s_sm_dark[5] = {0};   // ISR-only working baselines
static uint32_t          s_sm_work[5] = {0};   // ISR-only set under construction

// Published set + seqlock. The ISR bumps s_sm_seq either side of the copy;
// a reader that sees the same (even) value before and after has a set that
// was not being rewritten while it read it.
static volatile uint32_t s_sm_pub[5] = {0};
static volatile uint32_t s_sm_seq = 0;

void Irs_SM_Reset(void)
{
	for (int i = 0; i < 4; ++i) Irs_Emitter_Set(SM_ORDER[i], 0);
	s_sm_state = SM_S_DARK;
}

// Same thing, but only when the sampler is actually armed -- called from
// Irs_Adc_Lock, which runs on every single blocking conversion, so it must be
// free when the sampler is off.
static void irs_sm_park(void)
{
	if (s_sm_on) Irs_SM_Reset();
}

void Irs_SM_Enable(uint8_t on)
{
	if (on) {
		Irs_SM_Reset();
		s_sm_on = 1;
	} else {
		s_sm_on = 0;
		// Leave the emitters dark so a disarmed sampler costs nothing and the
		// blocking path starts from the same clean state it always assumed.
		for (int i = 0; i < 4; ++i) Irs_Emitter_Set(SM_ORDER[i], 0);
	}
}

uint8_t Irs_SM_Enabled(void) { return s_sm_on; }

// Called once per tick from the 1 kHz control ISR. Returns immediately when
// disarmed, or when thread context is holding the ADC (that tick is simply
// skipped -- the sequence resumes where it left off, one tick later).
void Irs_Tick(void)
{
	if (!s_sm_on)    return;
	if (s_adc_lock)  return;   // thread context owns the converter this tick

	uint8_t st = s_sm_state;

	if (st == SM_S_DARK) {
		// All emitters have been off for a full tick: read the ambients.
		for (int i = 0; i < 4; ++i) {
			ADCSensors sen = SM_ORDER[i];
			s_sm_dark[sen] = adc_read_channel(sen);
		}
		Irs_Emitter_Set(SM_ORDER[0], 1);   // light the first emitter for next tick
		s_sm_state = 1;
		return;
	}

	// Lit read of SM_ORDER[st-1], settled since the previous tick.
	ADCSensors sen = SM_ORDER[st - 1];
	uint32_t lit = adc_read_channel(sen);
	Irs_Emitter_Set(sen, 0);
	s_sm_work[sen] = (lit > s_sm_dark[sen]) ? (lit - s_sm_dark[sen]) : 0;

	if (st < 4) {
		Irs_Emitter_Set(SM_ORDER[st], 1);  // light the next one for next tick
		s_sm_state = (uint8_t)(st + 1);
		return;
	}

	// Set complete -- publish it under the seqlock and go back to DARK.
	++s_sm_seq;                                  // odd: write in progress
	for (int i = 0; i < 5; ++i) s_sm_pub[i] = s_sm_work[i];
	++s_sm_seq;                                  // even: set is consistent
	s_sm_state = SM_S_DARK;
}

// Copy the most recent complete set. Returns its sequence number, or 0 if the
// sampler has not published one yet (caller should fall back to a blocking
// read). Safe to call from thread context while the ISR is running.
uint32_t Irs_Get_Latest(uint32_t out[5])
{
	for (;;) {
		uint32_t s1 = s_sm_seq;
		if (s1 & 1u) continue;                   // mid-publish, retry
		for (int i = 0; i < 5; ++i) out[i] = s_sm_pub[i];
		if (s_sm_seq == s1) return s1 >> 1;      // stable -> completed-set count
	}
}
