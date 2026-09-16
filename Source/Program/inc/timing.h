/******************************************************************************
 * timing.h -- how long a move actually takes.
 *
 * The unit-cost flood answers "fewest cells". To answer "least time" the
 * planner needs a cost in milliseconds, and the trap is that TIME IS NOT
 * ADDITIVE PER CELL: she accelerates, so four one-cell moves and one four-cell
 * straight are not the same. A per-cell number cannot express that.
 *
 * What restores additivity is the turn table. Every turn in turn_params runs at
 * a CONSTANT forward speed, so a straight between two turns has a known entry
 * speed and a known exit speed, and its duration is a closed form of
 * (length, v_in, v_out, v_max, accel). Segment costs then no longer depend on
 * each other, which is exactly the condition a shortest-path search needs.
 *
 * Header-only and free of hardware, so it compiles both in CubeIDE and in a
 * native test harness.
 *****************************************************************************/
#pragma once

#include <cmath>

namespace timing {

/// Local min so the planner needs no <algorithm> on the target.
inline float fminf_(float a, float b) { return a < b ? a : b; }

constexpr float INF_TIME = 1e9f;

/// Straight-line motion limits. Separate from the turn table because a
/// straight is the only place she is free to choose her speed.
struct MotionModel {
  float v_max;   // mm/s   top speed on a straight
  float accel;   // mm/s^2 forward acceleration
  float decel;   // mm/s^2 braking (positive number)
};

/**
 * Time in SECONDS to cover `dist` mm starting at v0 and arriving at exactly v1,
 * never exceeding v_max.
 *
 * Returns INF_TIME when the move is impossible -- there is not enough room to
 * get from v0 to v1 at all. That is a real answer, not an error: it is how the
 * planner discovers that a fast turn cannot be entered straight out of another
 * one, and rejects that pairing instead of quietly producing a route she cannot
 * drive.
 */
inline float straight_time(const MotionModel &m, float dist, float v0, float v1) {
  if (dist < 0.0f) return INF_TIME;
  if (dist < 1e-6f) return (std::fabs(v0 - v1) < 1e-3f) ? 0.0f : INF_TIME;

  // Shortest distance in which v0 can become v1 at full accel/decel.
  const float need = (v1 > v0) ? (v1 * v1 - v0 * v0) / (2.0f * m.accel)
                               : (v0 * v0 - v1 * v1) / (2.0f * m.decel);
  if (need > dist + 1e-6f) return INF_TIME;   // cannot be done in the space available

  // Peak speed of a pure accelerate-then-brake (triangular) profile.
  const float vp2 = (2.0f * m.accel * m.decel * dist + m.decel * v0 * v0 + m.accel * v1 * v1) /
                    (m.accel + m.decel);
  float vp = std::sqrt(vp2 > 0.0f ? vp2 : 0.0f);

  if (vp <= m.v_max) {                         // triangular: never reaches the cap
    return (vp - v0) / m.accel + (vp - v1) / m.decel;
  }
  // Trapezoidal: accelerate to v_max, hold, then brake.
  const float d_acc = (m.v_max * m.v_max - v0 * v0) / (2.0f * m.accel);
  const float d_dec = (m.v_max * m.v_max - v1 * v1) / (2.0f * m.decel);
  const float d_cruise = dist - d_acc - d_dec;
  return (m.v_max - v0) / m.accel + d_cruise / m.v_max + (m.v_max - v1) / m.decel;
}

/**
 * Time in SECONDS for a turn, from the same three numbers the turn table
 * already stores: total angle, peak angular velocity, angular acceleration.
 * Trapezoidal in omega, falling back to triangular when the turn is too small
 * to reach the peak.
 *
 * Note this does NOT need measuring on the bench -- it falls out of the table.
 * What does need measuring is whether the table's omega and alpha are the ones
 * she actually achieves.
 */
inline float turn_time(float angle_deg, float omega, float alpha) {
  const float theta = std::fabs(angle_deg);
  if (theta < 1e-6f) return 0.0f;
  if (omega <= 0.0f || alpha <= 0.0f) return INF_TIME;

  const float theta_ramp = (omega * omega) / alpha;   // angle used by both ramps
  if (theta >= theta_ramp) {
    return 2.0f * omega / alpha + (theta - theta_ramp) / omega;
  }
  return 2.0f * std::sqrt(theta / alpha);             // triangular, never reaches omega
}

/// Time for an in-place spin: she stops, spins, and moves off again. The
/// forward cost of stopping and restarting is carried by the straights either
/// side (they end and start at zero), so this is the spin alone.
inline float spin_time(float angle_deg, float omega, float alpha) {
  return turn_time(angle_deg, omega, alpha);
}

}  // namespace timing
