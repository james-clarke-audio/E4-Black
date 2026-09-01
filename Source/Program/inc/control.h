/*
 * control.h
 *
 * The 1 kHz motion control layer. Ties together the profiles (forward /
 * rotation), the odometry, and the motor controller, and is driven from the
 * SysTick interrupt via control_isr().
 *
 * A small mode machine keeps bring-up safe:
 *   CTRL_IDLE        - motors off, odometry still updating (watch counts)
 *   CTRL_OPEN_LOOP   - fixed test voltages (polarity check / characterisation)
 *   CTRL_CLOSED_LOOP - profiles drive the wheels through the controller
 */
#ifndef CONTROL_H
#define CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called from SysTick_Handler at 1 kHz. Safe before control_begin()
// (does nothing until enabled).
void control_isr(void);

#ifdef __cplusplus
}  // extern "C"

// ---- C++ API (called from app_main) ----

// Initialise odometry + motors and arm the ISR in IDLE (motors stay off).
void control_begin();

// Force IDLE: stop motors, disable closed-loop output.
void control_set_idle();

// Feed the latest battery voltage (read with the blocking ADC in the main
// loop, never in the ISR) to the motor controller for PWM compensation.
void control_update_battery(float v);

// Open-loop: apply fixed left/right volts for `ms` milliseconds, then stop.
// Blocking. Odometry is zeroed first so you can read the resulting distance/
// angle. Use for polarity verification and speed-vs-volts characterisation.
void control_open_loop_pulse(float left_volts, float right_volts, uint32_t ms);

// Closed-loop straight move. Blocking; aborts if either button is pressed.
void control_forward_move(float distance, float top_speed,
                          float final_speed, float acceleration);

// Closed-loop in-place spin through `angle` degrees (rate units are deg/s,
// deg/s/s). forward is held idle so the motion is pure rotation. Blocking;
// aborts on a fresh button press.
void control_spin(float angle, float top_omega, float final_omega, float alpha);

// Integrated (curved) turn: hold forward speed `v` (mm/s) while the gyro-
// stabilised rotation profile sweeps `angle` deg. Minimum radius = v/omega_max.
// angle sign: + = CCW / left. Blocking; a fresh button press aborts.
void control_arc_turn(float v, float lead_in, float angle,
                      float omega_max, float alpha, float lead_out);

// --- Maze-app telemetry (dead-reckoned pose + live stream) ---
void  control_pose_reset();          // re-centre pose at the start cell, heading 0
void  control_pose_set(float x, float y, float heading);  // SIM: place the pose directly (no motors)
float control_pose_x();              // mm from SW corner
float control_pose_y();
float control_pose_heading();        // deg, 0=N, CCW+ (absolute; survives move resets)
void  control_stream_telemetry();    // emit one POS + TEL pair over the link

// --- Bench test-mode (runtime) ---
void  control_set_test_mode(bool on);  // arm/disarm the POSE_TEST_*_OFFSET
bool  control_test_mode();             // current state (default false = real start)
void  control_recalibrate_gyro();      // safe re-cal of the gyro bias (hold still)

// --- Persistent run mode for the maze solver (motion.cpp / mouse.cpp) ---
void control_drive_reset();  // zero profiles/odometry, re-arm controllers, stay in closed loop
void control_run_begin();    // enter a run from idle (also zeroes the heading reference)
void control_run_end();      // leave the run: idle, motors off

#endif  // __cplusplus
#endif  // CONTROL_H
