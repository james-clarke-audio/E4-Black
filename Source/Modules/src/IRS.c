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
void Delay() { //this is how you can do a delay, modify the DELAY_COUNT in the .h file if your readings aren't consistent
	volatile uint32_t counter = DELAY_COUNT;
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
const float BATTERY_MULTIPLIER = (ADC_REF_VOLTS / ADC_FSR / BATTERY_DIVIDER_RATIO);

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
			Delay();
			// Get reading and turn of LED
			value = Analog_Read(ir);
			HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
			// Wait again...
			Delay();
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

// Ambient-subtracted read: sample dark (emitter off), pulse the emitter, sample
// lit, subtract. Rejects room/IR ambient so wall thresholds are stable. Clamped
// at 0. This is what the wall-sensing layer should use (not the raw Irs_Read).
uint32_t Irs_Read_Diff(ADCSensors ir)
{
	GPIO_TypeDef *port; uint16_t pin;
	if (!ir_port_pin(ir, &port, &pin)) return Analog_Read(ir);   // battery: raw
	uint32_t dark = Analog_Read(ir);            // ambient (emitter off)
	HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET); // emitter on
	Delay();
	uint32_t lit = Analog_Read(ir);             // ambient + reflected
	HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
	Delay();
	return (lit > dark) ? (lit - dark) : 0;
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
	ADC_HandleTypeDef *hadc1_ptr = Get_HAdc1_Ptr(); //this is a pointer to your hal_adc, you will need this when you call HAL_ADC_PollForConversion
	//this pointer will also be used to read the analog value, val = HAL_ADC_GetValue(hadc1_ptr);

	sConfig.Channel = channel;
	sConfig.Rank = 1;
	sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
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
