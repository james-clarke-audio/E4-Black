/*
 * motors_ctrl.cpp  -- see motors_ctrl.h
 *
 * Ported from mazerunner-core motors.h, using E4's own config constants
 * (FWD_KP/KD, ROT_KP/KD, SPEED_FF, BIAS_FF, MOTOR_*_POLARITY, LOOP_*). The
 * controller works in "volts"; each tick it converts to a battery-compensated
 * PWM duty in [-1000, +1000] for Motor_SetDuty_*(), which handles H-bridge
 * direction from the sign.
 */
#include "motors_ctrl.h"
#include "motion_config.h"   // ACC_FF, MAX_MOTOR_VOLTS, MOTOR_MAX_PWM (+ config.h)
#include "odometry.h"
#include "gyro.h"
#include "PWM.h"             // Motor_SetDuty_Left/Right(int16_t)

MotorController motors;

static inline float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
static inline int clampi(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void MotorController::begin() {
  reset_controllers();
  stop();
}

void MotorController::reset_controllers() {
  m_old_left_speed = 0;
  m_old_right_speed = 0;
  m_fwd_error = 0;
  m_rot_error = 0;
  m_previous_fwd_error = 0;
  m_previous_rot_error = 0;
}

void MotorController::stop() {
  set_left_motor_volts(0);
  set_right_motor_volts(0);
}

float MotorController::position_controller() {
  float increment = m_velocity * LOOP_INTERVAL;
  m_fwd_error += increment - odometry.robot_fwd_change();
  float diff = m_fwd_error - m_previous_fwd_error;
  m_previous_fwd_error = m_fwd_error;
  return FWD_KP * m_fwd_error + FWD_KD * diff;
}

float MotorController::angle_controller(float steering_adjustment) {
  float increment = m_omega * LOOP_INTERVAL;
  // Heading feedback from the gyro (clean yaw rate), not the scrub-noisy
  // encoder-derived rotation. Distance still comes from the encoders.
  m_rot_error += increment - gyro.rate_change();
  m_rot_error += steering_adjustment;
  float diff = m_rot_error - m_previous_rot_error;
  m_previous_rot_error = m_rot_error;
  return ROT_KP * m_rot_error + ROT_KD * diff;
}

float MotorController::leftFeedForward(float speed) {
  float ff = speed * SPEED_FF;
  if (speed > 0)      ff += BIAS_FF;
  else if (speed < 0) ff -= BIAS_FF;
  float acc = (speed - m_old_left_speed) * LOOP_FREQUENCY;
  m_old_left_speed = speed;
  ff += ACC_FF * acc;
  return ff;
}

float MotorController::rightFeedForward(float speed) {
  float ff = speed * SPEED_FF;
  if (speed > 0)      ff += BIAS_FF;
  else if (speed < 0) ff -= BIAS_FF;
  float acc = (speed - m_old_right_speed) * LOOP_FREQUENCY;
  m_old_right_speed = speed;
  ff += ACC_FF * acc;
  return ff;
}

void MotorController::update_controllers(float velocity, float omega,
                                         float steering_adjustment) {
  m_velocity = velocity;
  m_omega = omega;
  float pos_output = position_controller();
  float rot_output = angle_controller(steering_adjustment);
  float left_output  = pos_output - rot_output;
  float right_output = pos_output + rot_output;

  float tangent_speed = DEG2RAD(m_omega) * MOUSE_RADIUS;   // deg/s -> mm/s
  float left_speed  = m_velocity - tangent_speed;
  float right_speed = m_velocity + tangent_speed;
  if (m_feedforward_enabled) {
    left_output  += leftFeedForward(left_speed);
    right_output += rightFeedForward(right_speed);
  }
  if (m_controller_output_enabled) {
    set_right_motor_volts(right_output);
    set_left_motor_volts(left_output);
  }
}

int MotorController::pwm_compensated(float desired_volts, float battery_volts) {
  if (battery_volts < 1.0f) battery_volts = 1.0f;   // guard div-by-small
  return (int)(MOTOR_MAX_PWM * desired_volts / battery_volts);
}

void MotorController::set_left_motor_volts(float volts) {
  volts = clampf(volts, -MAX_MOTOR_VOLTS, MAX_MOTOR_VOLTS);
  m_left_motor_volts = volts;
  set_left_motor_pwm(pwm_compensated(volts, MOTOR_SUPPLY_VOLTS));
}

void MotorController::set_right_motor_volts(float volts) {
  volts = clampf(volts, -MAX_MOTOR_VOLTS, MAX_MOTOR_VOLTS);
  m_right_motor_volts = volts;
  set_right_motor_pwm(pwm_compensated(volts, MOTOR_SUPPLY_VOLTS));
}

void MotorController::set_left_motor_pwm(int pwm) {
  pwm = MOTOR_LEFT_POLARITY * clampi(pwm, -MOTOR_MAX_PWM, MOTOR_MAX_PWM);
  Motor_SetDuty_Left((int16_t)pwm);
}

void MotorController::set_right_motor_pwm(int pwm) {
  pwm = MOTOR_RIGHT_POLARITY * clampi(pwm, -MOTOR_MAX_PWM, MOTOR_MAX_PWM);
  Motor_SetDuty_Right((int16_t)pwm);
}
