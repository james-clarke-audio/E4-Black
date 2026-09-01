/*
 * odometry.cpp  -- see odometry.h
 */
#include "odometry.h"
#include "motion_config.h"   // brings config.h (PI, WHEEL_DIAMETER, etc.)
#include "encoders.h"        // C driver: Encoder_Read_Left/Right() -> raw uint16 CNT

Odometry odometry;

void Odometry::begin() {
  // Per-wheel scale from the 500 mm push-test (config.c); deg-per-mm from the
  // (empirically-calibrated) mouse radius.
  m_mm_per_count_left  = MM_PER_COUNT_LEFT;
  m_mm_per_count_right = MM_PER_COUNT_RIGHT;
  m_deg_per_mm = 180.0f / (2.0f * MOUSE_RADIUS * PI);
  reset();
}

void Odometry::reset() {
  m_robot_distance = 0;
  m_robot_angle    = 0;
  m_fwd_change     = 0;
  m_rot_change     = 0;
  m_total_left     = 0;
  m_total_right    = 0;
  m_last_left  = Encoder_Read_Left();
  m_last_right = Encoder_Read_Right();
}

void Odometry::update() {
  uint16_t left_now  = Encoder_Read_Left();
  uint16_t right_now = Encoder_Read_Right();

  // 16-bit wrap-safe signed delta (correct for any change < +/-32768 counts).
  int16_t d_left  = (int16_t)(left_now  - m_last_left);
  int16_t d_right = (int16_t)(right_now - m_last_right);
  m_last_left  = left_now;
  m_last_right = right_now;

  int left_counts  = ENCODER_LEFT_POLARITY  * (int)d_left;
  int right_counts = ENCODER_RIGHT_POLARITY * (int)d_right;
  m_total_left  += left_counts;
  m_total_right += right_counts;

  float left_change  = left_counts  * m_mm_per_count_left;
  float right_change = right_counts * m_mm_per_count_right;

  m_fwd_change = 0.5f * (right_change + left_change);
  m_robot_distance += m_fwd_change;
  m_rot_change = (right_change - left_change) * m_deg_per_mm;
  m_robot_angle += m_rot_change;
}

float Odometry::robot_distance()   { return m_robot_distance; }
float Odometry::robot_angle()      { return m_robot_angle; }
float Odometry::robot_speed()      { return LOOP_FREQUENCY * m_fwd_change; }
float Odometry::robot_omega()      { return LOOP_FREQUENCY * m_rot_change; }
float Odometry::robot_fwd_change() { return m_fwd_change; }
float Odometry::robot_rot_change() { return m_rot_change; }
int32_t Odometry::total_left()     { return m_total_left; }
int32_t Odometry::total_right()    { return m_total_right; }
