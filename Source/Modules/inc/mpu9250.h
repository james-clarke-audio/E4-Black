/*
 * mpu9250.h
 *
 *  Created on: Nov 2, 2022
 *      Author: jamesclarke
 */

#ifndef MPU925_H_
#define MPU925_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "main.h"
#include "spi.h"

void MPU_ReadRegister(uint8_t address, uint8_t *pRxData, uint16_t RxSize);
void MPU_WriteRegister(uint8_t address, uint8_t value);

uint8_t MPU_CheckWHOAMI(void);
void MPU_Initialise(void);
void MPU_GetData(int16_t* AccData, int16_t* GyroData);

void MPU_ReadAcceleration(int16_t *Accel_X_RAW
		 , int16_t *Accel_Y_RAW
		 , int16_t *Accel_Z_RAW
		 , float *Ax
		 , float *Ay
		 , float *Az);

void MPU_ReadGyro(int16_t *Gyro_X_RAW, int16_t *Gyro_Y_RAW, int16_t *Gyro_Z_RAW, float *Gx, float *Gy, float *Gz);

#define CS_SPI_Pin GPIO_PIN_12
#define CS_SPI_GPIO_Port GPIOB
#define SPI_TIMOUT_MS     1000


#ifdef __cplusplus
}
#endif

#endif /* MPU925_H_ */


