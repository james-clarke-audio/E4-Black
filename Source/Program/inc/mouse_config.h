/*
 * mouse_config.h  --  maze/motion geometry and search tuning for E4.
 *
 * Ported from mazerunner-core's robot config (full-size 180 mm classic maze,
 * matching e4-maze.html), adapted to E4's drivetrain and the turn geometry we
 * bench-tuned. C++-only (const/constexpr with internal linkage, header-safe).
 */
#ifndef MOUSE_CONFIG_H
#define MOUSE_CONFIG_H

#include "maze.h"   // Location, Heading, Direction, TurnParameters users

// --- Goal room ("one entrance to the centre" rule) -------------------------
// The classic full-size goal is a 2x2 room; GOAL_ROOM_X0/Y0 is its SW corner.
// Once the mouse enters it, the whole room is asserted (see Mouse::assert_goal_room).
const int GOAL_ROOM_X0 = 7;
const int GOAL_ROOM_Y0 = 7;

// --- Cell geometry (full-size classic maze) --------------------------------
const float FULL_CELL = 180.0f;
const float HALF_CELL = 90.0f;

// Distance from the backed-against-the-back-wall start to the first cell
// centre. E4: the axle sits 41 mm from the back boundary, centre is at 90 mm.
const float BACK_WALL_TO_CENTER = 49.0f;

// Position within a cell (measured from its back boundary) at which the mouse
// reads the walls of the cell it is about to enter, and re-syncs each step.
const float SENSING_POSITION = 170.0f;

// --- Search speeds / acceleration (mm/s, mm/s/s) ---------------------------
const float SEARCH_SPEED        = 300.0f;
const float SEARCH_ACCELERATION = 2000.0f;
const float SEARCH_TURN_SPEED   = 300.0f;   // forward speed held through a smooth turn

// --- In-place (spin) turn dynamics (deg/s, deg/s/s) ------------------------
const float OMEGA_SPIN_TURN = 360.0f;
const float ALPHA_SPIN_TURN = 3600.0f;

// --- Wall-sensor-dependent (inert until real sensors exist) ----------------
// With no sensors, get_front_sum() returns 0, so a huge trigger means the
// smooth turn is started purely by distance -- exactly what we want for now.
const int FRONT_REFERENCE      = 850;      // front-sum when centred against a wall ahead
const int EXTRA_WALL_ADJUST    = 5;        // trigger nudge when a side wall is present
const int TURN_THRESHOLD_SS90E = 1000000;  // effectively "never trigger by sensor"

// Parameters for one smooth (curved) search turn.
struct TurnParameters {
  int   speed;         // mm/s    - constant forward speed held during the turn
  int   entry_offset;  // mm      - distance from the cell-boundary pivot to turn start
  int   exit_offset;   // mm      - distance from the pivot to turn end
  float angle;         // deg     - total turn angle (+ = left/CCW)
  float omega;         // deg/s   - peak angular velocity
  float alpha;         // deg/s/s - angular acceleration
  int   trigger;       //         - front-sensor value that starts the turn early
};

// --- Smooth search-turn parameters, indexed by Mouse::TurnType -------------
// { speed, entry_offset(mm), exit_offset(mm), angle(deg), omega(deg/s),
//   alpha(deg/s/s), trigger }.  omega/alpha come from the E4 arc tuning
// (R ~= 100 mm at v = 300 mm/s -> ~170 deg/s). entry/exit offsets are a
// starting point -- tune on the bench like we did the arc turn.
const TurnParameters turn_params[4] = {
    { (int)SEARCH_TURN_SPEED, 100, 30,  90.0f, 170.0f, 2500.0f, TURN_THRESHOLD_SS90E }, // 0 SS90EL
    { (int)SEARCH_TURN_SPEED, 100, 30, -90.0f, 170.0f, 2500.0f, TURN_THRESHOLD_SS90E }, // 1 SS90ER
    { (int)SEARCH_TURN_SPEED, 100, 30,  90.0f, 170.0f, 2500.0f, TURN_THRESHOLD_SS90E }, // 2 SS90L
    { (int)SEARCH_TURN_SPEED, 100, 30, -90.0f, 170.0f, 2500.0f, TURN_THRESHOLD_SS90E }, // 3 SS90R
};

#endif // MOUSE_CONFIG_H
