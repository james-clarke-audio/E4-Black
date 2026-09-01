/*
 * motion.h  --  higher-level locomotion facade for the maze solver.
 *
 * A thin, mazerunner-compatible wrapper over the E4 forward/rotation profiles
 * and the motor controller, so mouse.cpp ports almost verbatim. It assumes a
 * "run" is active (control_run_begin() / control_drive_reset()): the 1 kHz ISR
 * ticks forward + rotation + motors, and these methods just start segments and
 * wait on them. Blocking waits abort on any button press.
 */
#ifndef MOTION_H
#define MOTION_H

#include "config.h"        // SWITCH_LEFT/RIGHT, HAL_Delay
#include "profile.h"       // forward, rotation
#include "motors_ctrl.h"   // motors
#include "control.h"       // control_drive_reset(), control_run_end(), control_stream_telemetry()

class Motion {
 public:
  // --- drive-system state -------------------------------------------------
  void reset_drive_system() { control_drive_reset(); }   // clean slate, run stays live
  void stop()               { motors.stop(); }
  void disable_drive()      { motors.disable_controllers(); }
  void emergency_stop()     { control_run_end(); }

  // --- forward channel ----------------------------------------------------
  float position()     { return forward.position(); }
  float velocity()     { return forward.speed(); }
  float acceleration() { return forward.acceleration(); }
  void  set_target_velocity(float v) { forward.set_target_speed(v); }

  void  start_move(float dist, float top, float fin, float acc) { forward.start(dist, top, fin, acc); }
  bool  move_finished() { return forward.is_finished(); }
  void  move(float dist, float top, float fin, float acc) {
    forward.start(dist, top, fin, acc);
    while (!forward.is_finished()) {
      if (SWITCH_LEFT() || SWITCH_RIGHT()) { emergency_stop(); return; }
      stream_periodic();
      HAL_Delay(2);
    }
  }

  void  set_position(float p)            { forward.set_position(p); }
  void  adjust_forward_position(float d) { forward.adjust_position(d); }

  // --- rotation channel ---------------------------------------------------
  float angle() { return rotation.position(); }
  float omega() { return rotation.speed(); }
  float alpha() { return rotation.acceleration(); }

  void  start_turn(float a, float top, float fin, float acc) { rotation.start(a, top, fin, acc); }
  bool  turn_finished() { return rotation.is_finished(); }
  void  turn(float a, float top, float fin, float acc) {
    rotation.start(a, top, fin, acc);
    while (!rotation.is_finished()) {
      if (SWITCH_LEFT() || SWITCH_RIGHT()) { emergency_stop(); return; }
      stream_periodic();
      HAL_Delay(2);
    }
  }

  // --- in-place spin: bleed forward speed to zero, then rotate ------------
  void spin_turn(float angle, float omega, float alpha) {
    forward.set_target_speed(0);
    while (forward.speed() != 0) {
      if (SWITCH_LEFT() || SWITCH_RIGHT()) { emergency_stop(); return; }
      stream_periodic();
      HAL_Delay(2);
    }
    rotation.reset();
    rotation.move(angle, omega, 0, alpha);
  }

  // --- utility motion (robot assumed already moving) ---------------------
  void stop_at(float pos)     { forward.move(pos - forward.position(), forward.speed(), 0, forward.acceleration()); }
  void stop_after(float dist) { forward.move(dist, forward.speed(), 0, forward.acceleration()); }

  void wait_until_position(float pos) {
    while (forward.position() < pos) {
      if (SWITCH_LEFT() || SWITCH_RIGHT()) { emergency_stop(); return; }
      stream_periodic();
      HAL_Delay(2);
    }
  }
  void wait_until_distance(float dist) { wait_until_position(forward.position() + dist); }

  // Push pose/telemetry to the app at ~20 Hz from inside the blocking
  // motion waits, so a search animates smoothly instead of jumping cells.
  void stream_periodic() {
    static uint32_t last = 0;
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last) >= 50) { last = now; control_stream_telemetry(); }
  }
};

extern Motion motion;

#endif  // MOTION_H
