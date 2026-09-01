/*
 * motion_config.h
 *
 * The robot's physical, gain and polarity constants ALREADY live in
 * config.h / config.c (WHEEL_DIAMETER, ENCODER_PULSES, GEAR_RATIO, MOUSE_RADIUS,
 * FWD_KP/KD, ROT_KP/KD, SPEED_FF, BIAS_FF, ENCODER_*_POLARITY, MOTOR_*_POLARITY,
 * LOOP_FREQUENCY, LOOP_INTERVAL). That is the single place to tune the robot.
 *
 * This header only adds the few constants the 1 kHz control code needs that
 * config does not (yet) define. C++-only. constexpr, so header-safe across TUs.
 */
#ifndef MOTION_CONFIG_H
#define MOTION_CONFIG_H

#include "config.h"   // all the tunables + PI + DEG2RAD()

// Acceleration feedforward gain. config has SPEED_FF and BIAS_FF but no accel
// term; 0 disables it (SPEED_FF + BIAS_FF + PD is plenty to get moving).
// Set to (FWD_TM / FWD_KM) once you have characterised the motor, to sharpen
// the response.
constexpr float ACC_FF = 0.0f;

// Drive ceiling for a 1S LiPo (keep under the pack voltage so PWM does not
// just saturate). MOTOR_MAX_PWM matches the [-1000, +1000] duty range of the
// existing Motor_SetDuty_*() driver.
// Regulated motor-rail voltage from the buck/boost converter that feeds the
// DRV8833 -- this is the PWM-compensation reference (a constant, since a
// regulated rail does not sag with the battery). Confirmed: 9 V rail.
constexpr float MOTOR_SUPPLY_VOLTS = 9.0f;

// Controller output ceiling. Kept well under the rail for a gentle bring-up;
// raise toward the rail once the motors' voltage rating is known.
constexpr float MAX_MOTOR_VOLTS = 4.0f;
constexpr int   MOTOR_MAX_PWM   = 1000;

#endif // MOTION_CONFIG_H
