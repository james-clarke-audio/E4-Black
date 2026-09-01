/*
 * motors_ctrl.h
 *
 * The E4 motor controller: a feedforward + PD scheme ported from mazerunner
 * motors.h. It is fed a forward velocity and a rotation rate (omega) each
 * control tick and produces left/right motor drive, using odometry for
 * feedback and the battery voltage for PWM compensation.
 *
 * The class name is MotorController to avoid clashing with the existing C
 * "Motor_*" driver (PWM.h), which it calls to actually move the wheels.
 *
 * SAFETY: output starts DISABLED. Closed-loop drive only happens once
 * enable_controllers() has been called (the control layer does this when it
 * enters CLOSED_LOOP mode). The low-level set_*_motor_volts() are always live,
 * for the open-loop bring-up test.
 */
#ifndef MOTORS_CTRL_H
#define MOTORS_CTRL_H

class MotorController {
 public:
  void begin();
  void reset_controllers();

  void enable_controllers()  { m_controller_output_enabled = true; }
  void disable_controllers() { m_controller_output_enabled = false; }
  void enable_feedforward()  { m_feedforward_enabled = true; }
  void disable_feedforward() { m_feedforward_enabled = false; }

  void stop();   // 0 volts to both motors (coast)

  // Fed from the main loop (blocking ADC must not run in the ISR).
  void set_battery_voltage(float v) { m_battery_volts = v; }
  float battery_voltage()           { return m_battery_volts; }

  // Main closed-loop entry, called each control tick.
  void update_controllers(float velocity, float omega, float steering_adjustment);

  // Low-level open-loop drive (bring-up / characterisation). Always active.
  void set_left_motor_volts(float volts);
  void set_right_motor_volts(float volts);

  float get_left_motor_volts()  { return m_left_motor_volts; }
  float get_right_motor_volts() { return m_right_motor_volts; }

 private:
  float position_controller();
  float angle_controller(float steering_adjustment);
  float leftFeedForward(float speed);
  float rightFeedForward(float speed);
  int   pwm_compensated(float desired_volts, float battery_volts);
  void  set_left_motor_pwm(int pwm);
  void  set_right_motor_pwm(int pwm);

  bool  m_controller_output_enabled = false;  // start SAFE
  bool  m_feedforward_enabled       = true;
  float m_previous_fwd_error = 0, m_previous_rot_error = 0;
  float m_fwd_error = 0, m_rot_error = 0;
  float m_velocity = 0, m_omega = 0;
  float m_old_left_speed = 0, m_old_right_speed = 0;
  float m_left_motor_volts = 0, m_right_motor_volts = 0;
  float m_battery_volts = 3.7f;   // sensible 1S default until first ADC read
};

extern MotorController motors;

#endif // MOTORS_CTRL_H
