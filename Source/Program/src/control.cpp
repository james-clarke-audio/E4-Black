/*
 * control.cpp  -- see control.h
 */
#include "control.h"
#include "config.h"        // SWITCH_LEFT/RIGHT macros, HAL_Delay
#include "motion_config.h"
#include "odometry.h"
#include "gyro.h"
#include "report.h"
#include "motors_ctrl.h"
#include "profile.h"       // extern Profile forward, rotation
#include <math.h>

enum CtrlMode { CTRL_IDLE = 0, CTRL_OPEN_LOOP, CTRL_CLOSED_LOOP };

static volatile CtrlMode s_mode        = CTRL_IDLE;
static volatile bool     s_enabled     = false;
static volatile float    s_open_left_v = 0.0f;
static volatile float    s_open_right_v= 0.0f;

// Dead-reckoned pose for the maze app. Absolute heading integrated separately
// from gyro.angle() so the per-move gyro.reset_angle() calls don't disturb it.
static volatile float s_pose_x = 90.0f, s_pose_y = 90.0f, s_pose_heading = 0.0f;

// Runtime bench test-mode: when true, control_pose_reset() applies the
// POSE_TEST_*_OFFSET so a move can be run from an on-maze spot. Default OFF
// (real column-0 start). Toggled from the menu; never persisted.
static bool s_test_mode = false;

// ---------------------------------------------------------------------------
// The 1 kHz control ISR. Keep it short and non-blocking. No HAL_Delay, no
// blocking ADC (battery is fed in from the main loop).
// ---------------------------------------------------------------------------
extern "C" void control_isr(void) {
  if (!s_enabled) {
    return;
  }
  odometry.update();                       // always keep localisation live
  gyro.update();                           // integrate yaw heading (drives rotation control)
  // Dead-reckon absolute pose (mm) for the maze app.
  s_pose_heading += gyro.rate_change();
  {
    float ds = odometry.robot_fwd_change();
    float th = DEG2RAD(s_pose_heading);
    s_pose_x += -ds * sinf(th);
    s_pose_y +=  ds * cosf(th);
  }
  switch (s_mode) {
    case CTRL_IDLE:
      break;                               // motors already stopped
    case CTRL_OPEN_LOOP:
      motors.set_left_motor_volts(s_open_left_v);
      motors.set_right_motor_volts(s_open_right_v);
      break;
    case CTRL_CLOSED_LOOP:
      forward.update();
      rotation.update();
      motors.update_controllers(forward.speed(), rotation.speed(), 0.0f);
      break;
  }
}

void control_begin() {
  odometry.begin();
  gyro.begin();          // ~600 ms zero-rate calibration -- hold still
  motors.begin();
  forward.reset();
  rotation.reset();
  control_pose_reset();
  s_mode = CTRL_IDLE;
  s_enabled = true;        // ISR live from here, but IDLE keeps motors off
  report_write("RST\r\n");  // tell the maze app to start a fresh view
}

void control_set_idle() {
  s_mode = CTRL_IDLE;
  motors.disable_controllers();
  motors.stop();
}

// --- Persistent run mode for the maze solver (motion.cpp / mouse.cpp) -------
// Unlike the one-shot control_forward_move / _spin / _arc_turn calls, a "run"
// keeps the closed loop engaged across many profile segments so the mouse
// brain can start moves/turns and react at trigger points while the ISR ticks
// the forward + rotation profiles and the motor controller.

void control_drive_reset() {
  // Clean slate ready to move WITHOUT ending the run: profiles + odometry
  // zeroed and controllers re-armed, but the closed loop stays engaged.
  // Heading (gyro) is left running so it is continuous across the whole run.
  motors.stop();
  odometry.reset();
  forward.reset();
  rotation.reset();
  motors.reset_controllers();
  motors.enable_controllers();
  s_mode = CTRL_CLOSED_LOOP;
}

void control_run_begin() {
  // Enter a run from idle: zero the heading reference, then drive-reset.
  gyro.reset_angle();
  control_drive_reset();
}

void control_run_end() {
  s_mode = CTRL_IDLE;
  motors.disable_controllers();
  motors.stop();
}

void control_update_battery(float v) {
  motors.set_battery_voltage(v);
}

void control_open_loop_pulse(float left_volts, float right_volts, uint32_t ms) {
  motors.reset_controllers();
  odometry.reset();
  s_open_left_v  = left_volts;   // set volts BEFORE switching mode
  s_open_right_v = right_volts;
  s_mode = CTRL_OPEN_LOOP;
  HAL_Delay(ms);
  s_mode = CTRL_IDLE;
  motors.stop();
}

void control_forward_move(float distance, float top_speed,
                          float final_speed, float acceleration) {
  // Wait for the triggering button to be released first -- otherwise the
  // abort check in the loop below fires immediately on the same press that
  // started this move, and it stops before it moves.
  while (SWITCH_LEFT() || SWITCH_RIGHT()) {
    HAL_Delay(5);
  }
  HAL_Delay(1000);  // hands-off settle: let go of the mouse before it moves

  odometry.reset();
  gyro.reset_angle();
  forward.reset();
  rotation.reset();
  motors.reset_controllers();
  motors.enable_controllers();
  s_mode = CTRL_CLOSED_LOOP;
  forward.start(distance, top_speed, final_speed, acceleration);
  uint32_t t_tel = HAL_GetTick();
  while (!forward.is_finished()) {
    if (SWITCH_LEFT() || SWITCH_RIGHT()) {   // manual abort
      forward.stop();
      break;
    }
    if (HAL_GetTick() - t_tel >= 50) { t_tel = HAL_GetTick(); control_stream_telemetry(); }
    HAL_Delay(2);
  }
  HAL_Delay(60);           // let the controller settle at the stop point
  s_mode = CTRL_IDLE;
  motors.disable_controllers();
  motors.stop();
}


void control_spin(float angle, float top_omega, float final_omega, float alpha) {
  // Wait for the triggering button to be released (same reason as the move).
  while (SWITCH_LEFT() || SWITCH_RIGHT()) {
    HAL_Delay(5);
  }
  HAL_Delay(1000);  // hands-off settle: let go of the mouse before it moves

  odometry.reset();
  gyro.reset_angle();
  forward.reset();      // held idle -> forward velocity stays 0
  rotation.reset();
  motors.reset_controllers();
  motors.enable_controllers();
  s_mode = CTRL_CLOSED_LOOP;
  rotation.start(angle, top_omega, final_omega, alpha);
  uint32_t t_log = HAL_GetTick();
  while (!rotation.is_finished()) {
    if (SWITCH_LEFT() || SWITCH_RIGHT()) {   // fresh press aborts
      rotation.stop();
      break;
    }
    if (HAL_GetTick() - t_log >= 50) {       // stream telemetry during the spin
      t_log = HAL_GetTick();
      control_stream_telemetry();
    }
    HAL_Delay(2);
  }
  HAL_Delay(400);   // hold closed-loop so the gyro catches up to the target before stopping
  s_mode = CTRL_IDLE;
  motors.disable_controllers();
  motors.stop();
}

void control_arc_turn(float v, float lead_in, float angle,
                      float omega_max, float alpha, float lead_out) {
  while (SWITCH_LEFT() || SWITCH_RIGHT()) {   // wait for release
    HAL_Delay(5);
  }
  HAL_Delay(1000);   // hands-off settle

  odometry.reset();
  gyro.reset_angle();
  forward.reset();
  rotation.reset();
  motors.reset_controllers();
  motors.enable_controllers();
  s_mode = CTRL_CLOSED_LOOP;

  forward.start(100000.0f, v, v, 2000.0f);   // hold v through all three phases
  uint32_t t_tel = HAL_GetTick();
  bool aborted = false;

  // Phase 1: straight lead-in -- carry her into the cell before turning.
  while (odometry.robot_distance() < lead_in) {
    if (SWITCH_LEFT() || SWITCH_RIGHT()) { aborted = true; break; }
    if (HAL_GetTick() - t_tel >= 50) { t_tel = HAL_GetTick(); control_stream_telemetry(); }
    HAL_Delay(2);
  }
  // Phase 2: the arc -- forward speed held while the rotation profile sweeps.
  if (!aborted) {
    rotation.start(angle, omega_max, 0.0f, alpha);
    while (!rotation.is_finished()) {
      if (SWITCH_LEFT() || SWITCH_RIGHT()) { rotation.stop(); aborted = true; break; }
      if (HAL_GetTick() - t_tel >= 50) { t_tel = HAL_GetTick(); control_stream_telemetry(); }
      HAL_Delay(2);
    }
  }
  // Phase 3: straight lead-out -- settle onto the new lane. The heading
  // controller holds the final angle here, pulling out any turn overshoot.
  if (!aborted) {
    float d0 = odometry.robot_distance();
    while (odometry.robot_distance() - d0 < lead_out) {
      if (SWITCH_LEFT() || SWITCH_RIGHT()) { aborted = true; break; }
      if (HAL_GetTick() - t_tel >= 50) { t_tel = HAL_GetTick(); control_stream_telemetry(); }
      HAL_Delay(2);
    }
  }
  forward.stop();
  HAL_Delay(200);
  s_mode = CTRL_IDLE;
  motors.disable_controllers();
  motors.stop();
}

// --- maze-app pose + telemetry ---
void control_pose_reset() {
  // Real start (config.c) plus the bench offset only when test-mode is armed.
  s_pose_x = POSE_START_X + (s_test_mode ? POSE_TEST_X_OFFSET : 0.0f);
  s_pose_y = POSE_START_Y + (s_test_mode ? POSE_TEST_Y_OFFSET : 0.0f);
  s_pose_heading = 0.0f;     // facing North
}
void control_set_test_mode(bool on) { s_test_mode = on; }
void control_pose_set(float x, float y, float heading) {  // SIM: place the pose directly
  s_pose_x = x; s_pose_y = y; s_pose_heading = heading;
}
bool control_test_mode()            { return s_test_mode; }

// Safe runtime gyro re-calibration. The 1 kHz ISR reads the MPU over SPI2
// every tick, so we drop s_enabled (ISR becomes a no-op), wait out any
// in-flight tick, then let gyro.begin() own the bus for its ~600 ms bias
// measurement. Hold the mouse still. Odometry is zeroed on resume.
void control_recalibrate_gyro() {
  bool was = s_enabled;
  s_enabled = false;      // ISR now returns immediately -> SPI2 idle
  HAL_Delay(2);           // guarantee the current tick's ISR has finished
  gyro.begin();           // sole owner of SPI2 (blocking; zeroes heading)
  odometry.reset();       // drop any counts logged while paused
  s_enabled = was;        // resume the control loop
}
float control_pose_x()       { return s_pose_x; }
float control_pose_y()       { return s_pose_y; }
float control_pose_heading() { return s_pose_heading; }

void control_stream_telemetry() {
  report_pose(s_pose_x, s_pose_y, s_pose_heading);
  report_tel(HAL_GetTick(), odometry.robot_speed(), gyro.rate(),
             odometry.robot_distance(), odometry.robot_angle(),
             gyro.angle(), motors.battery_voltage());
}
