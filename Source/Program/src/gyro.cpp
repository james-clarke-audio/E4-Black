/*
 * gyro.cpp  -- see gyro.h
 */
#include "gyro.h"
#include "motion_config.h"   // config.h -> LOOP_INTERVAL, GYRO_POLARITY
#include "mpu9250.h"         // MPU_ReadRegister(), HAL

Gyro gyro;

// MPU-9250 GYRO_ZOUT_H register; a 2-byte burst read auto-increments to _L.
static const uint8_t GYRO_ZOUT_H = 0x47;
// Gyro configured for +/-2000 dps full scale -> 16.4 LSB per deg/s.
static const float DPS_PER_LSB = 1.0f / 16.4f;
// Zero-rate calibration: ~600 ms of samples at startup.
static const int CAL_SAMPLES = 300;

int16_t Gyro::read_z_raw() {
  uint8_t buf[2];
  MPU_ReadRegister(GYRO_ZOUT_H, buf, 2);
  return (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
}

void Gyro::begin() {
  // Average many samples at rest to find the zero-rate offset. The mouse MUST
  // be held still while this runs; it runs once, from control_begin().
  int32_t sum = 0;
  for (int i = 0; i < CAL_SAMPLES; i++) {
    sum += read_z_raw();
    HAL_Delay(2);
  }
  m_bias = (float)sum / (float)CAL_SAMPLES;
  reset_angle();
}

void Gyro::reset_angle() {
  m_angle = 0;
  m_rate_change = 0;
}

void Gyro::update() {
  float rate = GYRO_POLARITY * GYRO_SCALE * ((float)read_z_raw() - m_bias) * DPS_PER_LSB;
  m_rate = rate;
  m_rate_change = rate * LOOP_INTERVAL;
  m_angle += m_rate_change;
}

float Gyro::rate()        { return m_rate; }
float Gyro::angle()       { return m_angle; }
float Gyro::rate_change() { return m_rate_change; }
float Gyro::bias()        { return m_bias; }
