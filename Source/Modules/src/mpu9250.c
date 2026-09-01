/*
 * mpu9250.c
 *
 *  Created on: Nov 2, 2022
 *      Author: jamesclarke
 */

#include "MPU9250.h"

#define MPU_READ 0x80

#define BYTE 8
#define MPU_CAL_SAMPLE_NUM 100
#define MPU_AVERAGE_FACTOR 2
#define MPU_COMPLEMENT_2_FACTOR 2
#define MPU_CAL_SAMPLE_US 1000

#define MPU_SMPLRT_DIV 25
#define MPU_CONFIG 26
#define MPU_GYRO_CONFIG 27
#define MPU_SIGNAL_PATH_RESET 104
#define MPU_PWR_MGMT_1 107
#define MPU_USER_CTRL 106
#define MPU_WHOAMI 117

#define MPU_GYRO_ZOUT_H 71
#define MPU_GYRO_ZOUT_L 72
#define MPU_Z_OFFS_USR_H 23
#define MPU_Z_OFFS_USR_L 24

#define MPU_ACCEL_OUT 0x3B
#define MPU_ACCEL_XOUT_H_REG 0x43
#define	GYRO_XOUT_H_REG 0x3B

#define MPU_MASK_H 0xFF00
#define MPU_MASK_L 0x00FF

#define MPU_GYRO_SENSITIVITY_2000_DPS 16.4
#define MPU_DPS_TO_RADPS (PI / 180)

// global variable declaration
static uint8_t mpu_value[13]; // value[0] is dummy data
static uint8_t mpu_data[2];


void MPU_ReadRegister(uint8_t address, uint8_t *pRxData, uint16_t RxSize)
{
	HAL_GPIO_WritePin(CS_SPI_GPIO_Port, CS_SPI_Pin, GPIO_PIN_RESET);	//gpio_clear(GPIOB, GPIO12);
	uint8_t writeAddr = address | MPU_READ;
    HAL_SPI_Transmit(&hspi2, &writeAddr, 1, SPI_TIMOUT_MS);
    HAL_SPI_Receive(&hspi2, pRxData, RxSize, SPI_TIMOUT_MS);
	HAL_GPIO_WritePin(CS_SPI_GPIO_Port, CS_SPI_Pin, GPIO_PIN_SET);    //gpio_set(GPIOB, GPIO12);
}

void MPU_WriteRegister(uint8_t address, uint8_t value)
{
	HAL_GPIO_WritePin(CS_SPI_GPIO_Port, CS_SPI_Pin, GPIO_PIN_RESET);	//gpio_clear(GPIOB, GPIO12);
    HAL_SPI_Transmit(&hspi2, &address, 1, SPI_TIMOUT_MS);
    HAL_SPI_Transmit(&hspi2, &value, 1, SPI_TIMOUT_MS);
	HAL_GPIO_WritePin(CS_SPI_GPIO_Port, CS_SPI_Pin, GPIO_PIN_SET);    //gpio_set(GPIOB, GPIO12);
}

void setup_spi_low_speed(SPI_HandleTypeDef* spiHandle)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

void setup_spi_high_speed(SPI_HandleTypeDef* spiHandle)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

uint8_t MPU_CheckWHOAMI(void)
{
	uint8_t value;
	MPU_ReadRegister(MPU_WHOAMI, &value,1);
	return value;
}

void MPU_Initialise(void)
{
	setup_spi_low_speed(&hspi2);
	MPU_WriteRegister(MPU_PWR_MGMT_1, 0x80);
	HAL_Delay(1000);
	MPU_WriteRegister(MPU_SIGNAL_PATH_RESET, 0x07);
	HAL_Delay(1000);
	MPU_WriteRegister(MPU_USER_CTRL, 0x10);
	MPU_WriteRegister(MPU_SMPLRT_DIV, 0x00);
	MPU_WriteRegister(MPU_CONFIG, 0x00);
	MPU_WriteRegister(MPU_GYRO_CONFIG, 0x18);
	setup_spi_high_speed(&hspi2);
	HAL_Delay(1000);
}

/* read the data, each argiment should point to a array for x, y, and x */
void MPU_GetData(int16_t* AccData, int16_t* GyroData)
{
	// grab the data from the MPU9250
	MPU_ReadRegister(MPU_ACCEL_OUT, mpu_value, 13);

	// combine into 16 bit values
	AccData[0] = (((int16_t)mpu_value[0]) << 8) | mpu_value[1];
	AccData[1] = (((int16_t)mpu_value[2]) << 8) | mpu_value[3];
	AccData[2] = (((int16_t)mpu_value[4]) << 8) | mpu_value[5];
	GyroData[0] = (((int16_t)mpu_value[8]) << 8) | mpu_value[9];
	GyroData[1] = (((int16_t)mpu_value[10]) << 8) | mpu_value[11];
	GyroData[2] = (((int16_t)mpu_value[12]) << 8) | mpu_value[13];

}

void MPU_ReadAcceleration(int16_t *Accel_X_RAW, int16_t *Accel_Y_RAW, int16_t *Accel_Z_RAW, float *Ax, float *Ay, float *Az)
{
	MPU_ReadRegister(MPU_ACCEL_XOUT_H_REG, mpu_data, 2);
	*Accel_X_RAW = (int16_t)(mpu_data[0] << 8 | mpu_data[1]);

	MPU_ReadRegister(MPU_ACCEL_XOUT_H_REG+2, mpu_data, 2);
	*Accel_Y_RAW = (int16_t)(mpu_data[0] << 8 | mpu_data[1]);

	MPU_ReadRegister(MPU_ACCEL_XOUT_H_REG+4, mpu_data, 2);
	*Accel_Z_RAW = (int16_t)(mpu_data[0] << 8 | mpu_data[1]);

	*Ax = *Accel_X_RAW / 16384.0;
	*Ay = *Accel_Y_RAW / 16384.0;
	*Az = *Accel_Z_RAW / 16384.0;
}

void MPU_ReadGyro(int16_t *Gyro_X_RAW, int16_t *Gyro_Y_RAW, int16_t *Gyro_Z_RAW, float *Gx, float *Gy, float *Gz)
{
	MPU_ReadRegister(GYRO_XOUT_H_REG, mpu_data, 2);
	*Gyro_X_RAW = (int16_t)(mpu_data[0] << 8 | mpu_data[1]);

	MPU_ReadRegister(GYRO_XOUT_H_REG+2, mpu_data, 2);
	*Gyro_Y_RAW = (int16_t)(mpu_data[0] << 8 | mpu_data[1]);

	MPU_ReadRegister(GYRO_XOUT_H_REG+4, mpu_data, 2);
	*Gyro_Z_RAW = (int16_t)(mpu_data[0] << 8 | mpu_data[1]);

	*Gx = *Gyro_X_RAW / 178;
	*Gy = *Gyro_Y_RAW / 178;
	*Gz = *Gyro_Z_RAW / 178;
}
