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
			// Turn on LED
			HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
			// Wait a bit...
			Irs_Delay(IR_LIT_SETTLE_COUNT);
			// Get reading and turn of LED
			value = Analog_Read(ir);
			HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
			// Wait again...
			Irs_Delay(IR_DARK_SETTLE_COUNT);
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
	uint32_t dark = Analog_Read(ir);            // ambient (emitter off)
	HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET); // emitter on
	Irs_Delay(IR_LIT_SETTLE_COUNT);
	uint32_t lit = Analog_Read(ir);             // ambient + reflected
	HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
	Irs_Delay(IR_DARK_SETTLE_COUNT);
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
}

uint32_t Analog_Read(ADCSensors ir)
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
