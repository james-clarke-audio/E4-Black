/*
 * robot_sensors.h  --  the wall-sensor layer, with a runtime VIRTUAL/REAL switch.
 *
 *   VIRTUAL (use_real=false, the default): answers see_left/front/right_wall from
 *   a ground-truth maze ('truth') around the mouse's cell+heading, so the search
 *   explores in sim. get_front_sum() returns 0 -> smooth turns are distance-
 *   triggered and centring coasts.
 *
 *   REAL (use_real=true): reads the four IR detectors (ambient-subtracted, via
 *   Irs_Read_Diff) and sets the wall flags against tunable thresholds;
 *   get_front_sum() returns FL + FR (inner forward pair). Nothing in the brain
 *   changes - it calls the same update()/see_*_wall/get_front_sum() either way.
 *
 * Toggle at runtime from the menu (Sensors > Mode). The IR monitor streams the
 * raw ambient-subtracted values so the thresholds can be calibrated on a maze.
 */
#ifndef ROBOT_SENSORS_H
#define ROBOT_SENSORS_H
#include "config.h"   // SWITCH_LEFT/RIGHT, HAL_Delay
#include "maze.h"     // Location, Heading, left_from/right_from, is_exit
#include "IRS.h"      // Irs_Read_Diff, ADCSensors (IR_SIDE_LEFT ... IR_SIDE_RIGHT)

enum SteeringMode { STEERING_OFF, STEER_NORMAL, STEER_LEFT_WALL, STEER_RIGHT_WALL };
#define LEFT_START  0
#define RIGHT_START 1

extern Maze truth;    // ground-truth maze the virtual sensor reads (world.cpp)

class VirtualSensors {
 public:
  bool see_left_wall  = false;
  bool see_front_wall = false;
  bool see_right_wall = false;

  // false = virtual (read the ground-truth maze); true = real IR detectors.
  bool use_real = false;

  // Tunable wall-present thresholds on the ambient-subtracted reading. Set from
  // the IR monitor / calibration; sensible placeholders until the sensors exist.
  int thresh_side  = 60;   // side wall present when its diff exceeds this (SR right wall reads ~79)
  int thresh_front = 80;   // front wall present when (FL + FR) exceeds this

  // Last raw ambient-subtracted reads (for the monitor / calibration).
  int rd_left = 0, rd_fl = 0, rd_fr = 0, rd_right = 0;

  void set_steering_mode(SteeringMode) {}   // wall-follow steering: future
  void enable()  {}
  void disable() {}
  int  get_front_sum() { return m_front_sum; }

  // Sample all four detectors (ambient-subtracted) into rd_*. Always reads the
  // real hardware regardless of use_real -- used by the IR monitor.
  //
  // Two sources, same numbers. When the background sampler is armed the values
  // are simply latched from it (free, always <=5 ms old, safe to call while
  // driving). Otherwise -- or before it has published its first complete set --
  // this falls back to the blocking batched read, which is still the default
  // until the sampler is bench-proven.
  void sample_raw() {
    uint32_t d[5] = {0};
    if (!(Irs_SM_Enabled() && Irs_Get_Latest(d))) {
      Irs_Read_Diff_All(d);               // batched: one dark phase, then per-emitter lit
    }
    rd_left  = (int)d[IR_SIDE_LEFT];
    rd_fl    = (int)d[IR_FRONT_LEFT];
    rd_fr    = (int)d[IR_FRONT_RIGHT];
    rd_right = (int)d[IR_SIDE_RIGHT];
  }

  // Refresh the three relative wall flags. VIRTUAL: from the ground truth for
  // the mouse's current cell + heading. REAL: from the IR detectors (loc/hdg
  // ignored). The brain calls this before it reads the walls each cell.
  void update(Location loc, Heading hdg) {
    if (!use_real) {
      see_front_wall = !truth.is_exit(loc, hdg);
      see_left_wall  = !truth.is_exit(loc, left_from(hdg));
      see_right_wall = !truth.is_exit(loc, right_from(hdg));
      m_front_sum = 0;
      return;
    }
    sample_raw();
    // Mapping CONFIRMED by IR monitor (2 Sep 2026): a LEFT wall lights rd_left
    // (SL), a RIGHT wall lights rd_right (SR), a FRONT wall lights FL+FR. So the
    // OUTER pair SL/SR watch the SIDE walls; the INNER pair FL/FR watch FORWARD.
    // (The earlier SL/SR=front guess, from a verbal description, was backwards -
    // the hardware disagreed on a left-wall-only test. This is the original map.)
    m_front_sum    = rd_fl + rd_fr;           // FL + FR (inner, forward) = FRONT
    see_left_wall  = rd_left  > thresh_side;  // SL (outer) = LEFT wall
    see_right_wall = rd_right > thresh_side;  // SR (outer) = RIGHT wall
    see_front_wall = m_front_sum > thresh_front;
  }

  // Stand-in for the hand-over-sensor start: wait for (and release) a button.
  int wait_for_user_start() {
    while (!(SWITCH_LEFT() || SWITCH_RIGHT())) { HAL_Delay(5); }
    int side = SWITCH_LEFT() ? LEFT_START : RIGHT_START;
    while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }
    return side;
  }

 private:
  int m_front_sum = 0;
};
extern VirtualSensors sensors;

// Ground-truth maze builders (world.cpp)
void truth_clear();                            // reset to a fully-open known maze
void truth_set_cell(int x, int y, int mask);   // walls of one cell: N=1 E=2 S=4 W=8 (bit set = WALL)
void truth_load_default();                     // small bring-up maze: forces one right turn

// Ground-truth maze injection over BT. Returns true if the line was a GT
// command and handled it (emitting an ack). Called from the BT line reader.
bool maze_inject_line(const char *line);
#endif  // ROBOT_SENSORS_H
