/*
 * config.c
 *
 * Definitions for the constants declared extern in config.h.
 * Kept in one translation unit so the header can be included
 * everywhere without multiple-definition link errors.
 * This is the single place to edit the robot's tunable values.
 */
#include "config.h"

const uint8_t USER_MODE = (1);
const float WHEEL_DIAMETER = 23.0;   // nominal; effective rolling ~23.5 mm (tyre) per push-test
const float ENCODER_PULSES = 1024;   // IE2-512: 512 lines x 2 (STM32 timer x2 quadrature)
const float GEAR_RATIO = 4.0;        // 10T pinion : 40T gear (Faulhaber 1516T009 / IE2-512)
// Distance-per-count from the 500 mm push-test (avg of 3 runs: dTL~27733,
// dTR~27753). Measured empirically -- supersedes the WHEEL_DIAMETER /
// ENCODER_PULSES / GEAR_RATIO derivation, which predicted ~19% too many counts.
const float MM_PER_COUNT_LEFT  = 500.0f / 27733.0f;   // ~0.018029 mm/count
const float MM_PER_COUNT_RIGHT = 500.0f / 27753.0f;   // ~0.018016 mm/count
const float MOUSE_RADIUS = 30.6;

// Start pose: where the centre of rotation (the axle line) sits in the start
// cell (0,0) at power-on, using the competition start (Convention A): mouse
// aligned to the cell centre line and pushed back against the south wall.
//   X = cell centre (180/2 = 90)
//   Y = wall inner face (12mm wall / 2 = 6) + chassis back-to-axle (35) = 41
// Tune POSE_START_Y if the chassis / axle position changes. From here it is
// 90 - 41 = 49 mm forward to the cell centre (the initial centring move).
// These are the REAL competition start (column-0, against the back wall).
const float POSE_START_X = 90.0f;   // mm  (cell centre line, column 0)
const float POSE_START_Y = 41.0f;   // mm  (6mm wall face + 35mm back-to-axle)

// --- Bench testing offsets -------------------------------------------------
// Applied to the real start pose in control_pose_reset() ONLY when bench
// test-mode is armed at runtime (menu -> "Test mode ON"). Lets a move run from
// an on-maze spot (e.g. a LEFT turn needs room to the west). Default OFF, so a
// real run always starts at the true column-0 pose above.
const float POSE_TEST_X_OFFSET = 360.0f; // mm: 2 cells right (-> column 2) when test-mode on
const float POSE_TEST_Y_OFFSET = 0.0f;   // mm added to POSE_START_Y when test-mode on
const float ROTATION_BIAS = 0.0000;
const float FWD_KP = 2.0;
const float FWD_KD = 1.1;
const float ROT_KP = 2.1;
const float ROT_KD = 1.2;
const float STEERING_KP = 0.25;
const float STEERING_KD = 0.00;
const float STEERING_ADJUST_LIMIT = 10.0;
const float SPEED_FF = (1.0 / 280.0);
const float BIAS_FF = (23.0 / 280.0);
const int ENCODER_LEFT_POLARITY = (1);
const int ENCODER_RIGHT_POLARITY = (1);
const int MOTOR_LEFT_POLARITY = (-1);
const int MOTOR_RIGHT_POLARITY = (1);
const int GYRO_POLARITY = (1);
const float GYRO_SCALE = 1.02;   // trim so a commanded 360 = a true physical 360 (calibrate)

// Control-loop timing (1 kHz).
const float LOOP_FREQUENCY = 1000.0f;
const float LOOP_INTERVAL  = 0.001f;   // 1 / LOOP_FREQUENCY
