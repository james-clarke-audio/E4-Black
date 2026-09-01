/*
 * gyro.h
 *
 * Yaw-heading from the MPU-9250 Z-axis rate gyro (SPI2). Each control tick,
 * update() reads gyro-Z, subtracts a zero-rate bias measured at startup,
 * scales to deg/s and integrates a heading. A rate gyro measures the same
 * angular velocity everywhere on a rigid body, so the sensor's off-centre
 * mounting does not matter -- only its (flat, Z-up) orientation does.
 *
 * STEP 1 (this file) is observe-only: it does not yet feed the controller;
 * it just runs alongside the encoder odometry so the two headings can be
 * compared. Sign is set by GYRO_POLARITY (config.c), verified on the bench.
 */
#ifndef GYRO_H
#define GYRO_H

#include <stdint.h>

class Gyro {
 public:
  void  begin();        // measure zero-rate bias (HOLD STILL), zero heading
  void  update();       // per control tick: read Z, integrate heading
  void  reset_angle();  // zero the integrated heading

  float rate();         // deg/s, bias-corrected and signed
  float angle();        // integrated heading, deg
  float rate_change();  // deg this tick (used later for controller fusion)
  float bias();         // raw zero-rate offset (diagnostic)

 private:
  int16_t read_z_raw();
  float m_bias = 0;
  volatile float m_rate        = 0;
  volatile float m_angle       = 0;
  volatile float m_rate_change = 0;
};

extern Gyro gyro;

#endif // GYRO_H
