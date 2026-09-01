/*
 * odometry.h
 *
 * Localisation from the STM32 hardware encoders (TIM2 = left, TIM4 = right,
 * read via the C driver in encoders.h). Each control tick, update() takes the
 * wrap-safe change in each counter and integrates forward distance and heading.
 *
 * The mm-per-count and deg-per-mm scale factors are derived once in begin()
 * from the config values (WHEEL_DIAMETER, ENCODER_PULSES, GEAR_RATIO,
 * MOUSE_RADIUS), so recalibrating is just editing config.c.
 *
 * update() runs in the 1 kHz SysTick ISR; getters are called from the main
 * loop. Shared state is single 32-bit floats, whose load/store is atomic on
 * the Cortex-M4, so no critical section is needed for reads.
 */
#ifndef ODOMETRY_H
#define ODOMETRY_H

#include <stdint.h>

class Odometry {
 public:
  void begin();   // derive scale factors, latch counts, zero integrators
  void reset();   // zero integrators, re-latch counts (no motion jump)
  void update();  // call once per control tick (1 kHz)

  float robot_distance();    // mm
  float robot_angle();       // deg
  float robot_speed();       // mm/s
  float robot_omega();       // deg/s
  float robot_fwd_change();  // mm this tick
  float robot_rot_change();  // deg this tick

  int32_t total_left();      // raw counts, for the 500 mm calibration test
  int32_t total_right();

 private:
  volatile float m_robot_distance = 0;
  volatile float m_robot_angle    = 0;
  volatile float m_fwd_change     = 0;
  volatile float m_rot_change     = 0;
  volatile int32_t m_total_left   = 0;
  volatile int32_t m_total_right  = 0;
  uint16_t m_last_left  = 0;
  uint16_t m_last_right = 0;
  // scale factors derived from config in begin()
  float m_mm_per_count_left  = 0;
  float m_mm_per_count_right = 0;
  float m_deg_per_mm         = 0;
};

extern Odometry odometry;

#endif // ODOMETRY_H
