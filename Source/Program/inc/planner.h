/******************************************************************************
 * planner.h -- route planning over a KNOWN map, weighted by time.
 *
 * This does NOT replace the flood in maze.h. The flood is the right tool for
 * the search run: unit cost, re-run every cell, cheap, and robust to the map
 * changing underneath it. This is for the run after that, when the walls are
 * known and the question stops being "which way" and becomes "how fast".
 *
 * WHY IT IS NOT A FLOOD.  Gradient descent over a cost-per-cell array only
 * works while cost is a property of the cell. As soon as the cost of being
 * somewhere depends on which way you are pointing and how fast you are going,
 * there is no gradient to descend. So the graph changes shape:
 *
 *   node  = (cell, outgoing heading, speed class)   -- a TURN, not a cell
 *   edge  = "run straight N cells, then take the next turn"
 *   cost  = straight_time(...) + turn_time(...)
 *
 * Nodes exist only where she changes direction. A six-cell straight is one
 * edge, which is precisely what lets the cost model account for acceleration:
 * time is not additive per cell, so a per-cell number cannot express it.
 *
 * Two speed classes per node: she can take a corner as a smooth arc at the
 * turn's table speed, or stop and spin in place. Both are offered at every turn
 * and the search picks. That matters twice over -- it is how a tight section
 * correctly comes out as stop-and-spin, and it is what keeps the planner honest
 * when an arc simply cannot be entered in the space available.
 *
 * NO HEAP, NO RECURSION, NO VTABLES, ALMOST NO STACK. The working set lives in
 * file-static arrays in planner.cpp (~36 KB, listed there). The Route is
 * supplied BY THE CALLER rather than returned by value -- it is 1.2 KB, which
 * is more than a default Cortex-M stack has to spare, so on the robot it wants
 * to be a static or a member, not a local. The wall accessor is a function
 * pointer rather than a virtual, because a virtual destructor drags in
 * operator delete and that does not link on a no-heap build.
 *****************************************************************************/
#pragma once

#include <stdint.h>
#include "timing.h"

namespace plan {

constexpr int W = 16;
constexpr int H = 16;
constexpr int NHEAD = 4;          // N, E, S, W
constexpr int NSPEED = 2;         // 0 = stopped (spin), 1 = smooth arc speed
constexpr int NNODES = W * H * NHEAD * NSPEED;   // 2048
constexpr int MAX_STEPS = 80;     // turns in one route; the worst real maze uses ~51

enum Head : uint8_t { NN = 0, EE = 1, SS = 2, WW = 3 };
inline Head right_of(Head h) { return Head((h + 1) & 3); }
inline Head left_of(Head h)  { return Head((h + 3) & 3); }
inline Head back_of(Head h)  { return Head((h + 2) & 3); }

/// Everything the planner needs to know about the mouse. On the robot these are
/// filled from mouse_config.h and turn_params; kept as a plain struct so the
/// bench harness can drive it with hypothetical numbers and so that ramping
/// acceleration between runs is just a different Robot.
struct Robot {
  timing::MotionModel straight{300.0f, 2000.0f, 2000.0f};

  // Smooth 90 (SS90). Speed is held constant through the arc; the arc consumes
  // `offset` of path either side of the cell centre (offset ~= R = v / omega).
  float arc90_speed  = 300.0f;
  float arc90_offset = 100.0f;
  float arc90_omega  = 170.0f;
  float arc90_alpha  = 2500.0f;

  // Smooth 180 (SS180) -- the hairpin, not a dead end.
  float arc180_speed  = 300.0f;
  float arc180_offset = 90.0f;
  float arc180_omega  = 191.0f;
  float arc180_alpha  = 2500.0f;

  // In-place spin. The only row of the lot that has met a floor.
  float spin_omega = 360.0f;
  float spin_alpha = 3600.0f;

  bool allow_arc180 = true;   // SS180 needs the neighbouring column to be open

  // The clock starts as she LEAVES the start square, not when she moves, so the
  // run-up inside it is free time -- what crosses the line is the speed she has
  // built by then. BACK_WALL_TO_CENTER + HALF_CELL = 49 + 90.
  float start_run_up = 139.0f;

  // The clock stops as she ENTERS the target, so she never has to be stopped
  // there -- only able to stop within the run-out the goal box offers. That is
  // a constraint on entry speed, not a cost in the route.
  float goal_runout = 270.0f;
};

/// The only thing the planner asks of a map: "is this wall an exit?". A plain
/// function pointer and a context, deliberately -- an abstract base class would
/// be tidier to read and would put a vtable and an operator delete reference in
/// a build that has neither.
struct WallReader {
  bool (*is_exit)(const void *ctx, int x, int y, int heading);
  const void *ctx;
};

enum Move : uint8_t { MV_START, MV_ARC_L, MV_ARC_R, MV_ARC_180,
                      MV_SPIN_L, MV_SPIN_R, MV_SPIN_180, MV_GOAL };

struct Step {
  Move    move;      // the turn taken at the END of this step's straight
  uint8_t cells;     // cells of straight run before that turn
  uint8_t x, y;      // cell the turn happens in
  Head    heading;   // heading after the turn
  float   t;         // seconds for straight + turn
};

struct Route {
  bool    ok = false;
  bool    truncated = false;   // hit MAX_STEPS -- should never happen, say so if it does
  bool    overflow  = false;   // an edge outgrew the queue's ring: result is NOT valid
  float   seconds = 0.0f;
  int     cells = 0;
  int     turns = 0;           // smooth arcs
  int     spins = 0;           // stop-and-spin
  int     count = 0;
  Step    steps[MAX_STEPS];
};

enum Objective { SHORTEST, QUICKEST };

/// Plan from (sx,sy) heading start_head to any cell of the goal box, writing
/// the answer into `out`. SHORTEST minimises cells, breaking ties on time;
/// QUICKEST minimises time. Same graph and same code path, so the two are
/// directly comparable -- which is the point, because with v_max == turn speed
/// they come out as the same route.
///
/// `out` is ~1.2 KB. Give it static storage; do not make it a local.
void plan_route(Route &out, const WallReader &maze, const Robot &r, Objective obj,
                int sx, int sy, Head start_head,
                int gx, int gy, int gw = 2, int gh = 2);

}  // namespace plan
