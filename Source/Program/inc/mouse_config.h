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

// --- Wall follower ---------------------------------------------------------
// How many cells before she admits the hand she is following does not reach
// the goal.
//
// On a WALL-FOLLOWER course this never trips: those courses are built with a
// wall connected to the centre, so the hand always gets there. On a MAZE-
// SOLVER maze it always trips, because those are built with the inside
// disconnected from the outside on purpose. Same code, two events, and the
// limit is what lets the second one end in a sentence instead of a walk that
// never finishes. 512 is four times the cells in the arena.
const int FOLLOW_STEP_LIMIT = 512;

// --- Fast-run speeds (mm/s, mm/s/s) ---------------------------------------
// The SEARCH values above are what she explores at, and they are bounded by
// something real: she reads walls and decides where to go once per cell, so
// how fast she may search is a sensor question settled at the bench. The fast
// run has no such limit -- the map is already known -- and these are its
// numbers.
//
// Keeping them apart is what gives the planner a question to answer. While
// v_max equalled the turn speed she went as fast down a straight as round a
// corner, so SHORTEST and QUICKEST returned the same route to the millisecond
// and there was no "quickest" to find. Only when a straight can outrun a turn
// does a longer, straighter line start to beat a shorter, twistier one.
//
// RUN_DIAG_SPEED is deliberately lower. The diagonal corridor is 110.3 mm
// between posts against 168 mm down a cell, which leaves her about 14 mm a
// side with the plate and housings fitted rather than 43. She threads it; she
// does not charge it.
//
// NOT const: like the turn table and the spin dynamics these are measured, not
// chosen. Defined once in mouse.cpp, settable over BT with SPD, persisted in
// the EEPROM config block. The values there are the fallback.
//
// Nothing drives on these yet -- act_speed_run is still a stub. Today they are
// read by the planner alone, which is why a wrong value here costs a wrong
// estimate and not a wrecked mouse.
extern float RUN_SPEED;
extern float RUN_ACCELERATION;
extern float RUN_DIAG_SPEED;

// --- In-place (spin) turn dynamics (deg/s, deg/s/s) ------------------------
// NOT const, for the same reason the arcs are not: these are measured against
// a floor, not chosen. Defined once in mouse.cpp, written by the tuner,
// persisted in the EEPROM config block. The values there are the fallback.
//
// These are the ONLY turns that spin in place, and they are where the scrub
// comes from - two wheels a side off one pinion cannot pivot cleanly about the
// axle cross, so an in-place 90 costs about 3 mm of translation. An arc barely
// scrubs at all.
//
// That scrub is not avoidable at a dead end, and SS180 does NOT replace it: an
// SS180 sweeps a full cell pitch sideways, so it needs the neighbouring column
// open, and a dead end is walled on both sides and ahead. turn_back() stays
// exactly where it is. SS180 is for the hairpin - two corridors either side of
// a dividing wall, joined at the far end - which is a speed-run move, because
// committing to the arc means knowing the neighbour is open before entering
// the turn, and a search only learns that standing in the cell.
extern float OMEGA_SPIN_TURN;
extern float ALPHA_SPIN_TURN;

// --- Wall-sensor-dependent (inert until real sensors exist) ----------------
// With no sensors, get_front_sum() returns 0, so a huge trigger means the
// smooth turn is started purely by distance -- exactly what we want for now.
const int FRONT_REFERENCE      = 850;      // front-sum when centred against a wall ahead
const int EXTRA_WALL_ADJUST    = 5;        // trigger nudge when a side wall is present
const int TURN_THRESHOLD_SS90E = 1000000;  // effectively "never trigger by sensor"

// Parameters for one smooth (curved) search turn.
//
// entry_offset and exit_offset are NOT symmetric in meaning, and conflating
// them is an easy afternoon to lose:
//   entry_offset  where the arc STARTS, as a distance back from the pivot
//   exit_offset   a FRAME CORRECTION after the arc - nothing is driven, the
//                 position origin is relabelled so the next cell lines up
//   lead_out      a real straight run AFTER the arc, used only by the
//                 standalone menu move and the tuner (the search does not
//                 drive one: forward motion simply continues into the cell)
//
// The radius is not stored because it is not free: R = v / omega, with omega
// in radians. At 300 mm/s and 170 deg/s that is ~101 mm, which is why
// entry_offset is ~100 - for a turn centred on the crossing, the arc must
// start about R before it.
struct TurnParameters {
  int   speed;         // mm/s    - constant forward speed held during the turn
  int   entry_offset;  // mm      - distance from the cell-boundary pivot to turn start
  int   exit_offset;   // mm      - frame relabel after the turn (search only)
  int   lead_out;      // mm      - straight run after the arc (standalone move only)
  float angle;         // deg     - total turn angle (+ = left/CCW)
  float omega;         // deg/s   - peak angular velocity
  float alpha;         // deg/s/s - angular acceleration
  int   trigger;       //         - front-sensor value that starts the turn early
};

// --- Smooth search-turn parameters, indexed by Mouse::TurnType -------------
//
// NOT const, and THE ONLY definition of these turns anywhere. The menu 90
// moves, the search and the live tuner all read this array, and the tuner
// writes to it - so what you tune is what she runs. Before this they were
// three separate copies that had already drifted apart (the menu move used
// alpha 1000 while the search used 2500, which meant "Right 90" and the turn
// she made while searching were different turns).
//
// Persisted in the EEPROM config block, so a tune done at a venue survives a
// power cycle. Values here are the fallback when no saved block is found.
// Turn identities. Lives here rather than inside Mouse so the enum and the
// table it indexes cannot drift apart.
//
// WHY 45 AND 135 APPEAR TWICE. A 45 degree turn is not one turn. SD45 goes
// STRAIGHT ONTO the diagonal: it starts on a cell centreline and finishes on a
// diagonal. DS45 does the reverse. Same angle, mirrored geometry, different
// entry and exit offsets - one set of numbers cannot serve both, and using it
// for both is how a mouse ends up half a cell out only on alternate corners.
// DD90 turns a corner WITHOUT dropping off the diagonal, where the cell pitch
// is 180/sqrt2 = 127 mm rather than 180, so it is a much tighter turn.
enum TurnType {
  SS90EL   =  0,   // search 90, the only turns she makes today
  SS90ER   =  1,
  SS90L    =  2,   // fast straight-to-straight 90
  SS90R    =  3,
  SS180L   =  4,   // hairpin, not a dead end; R=90 lands one cell over
  SS180R   =  5,
  SD45L    =  6,   // straight ONTO the diagonal - gentle, so a large R
  SD45R    =  7,
  DS45L    =  8,   // diagonal back to straight - same angle, different geometry
  DS45R    =  9,
  SD135L   = 10,   // straight onto the diagonal, the long way round
  SD135R   = 11,
  DS135L   = 12,   // diagonal back to straight
  DS135R   = 13,
  DD90L    = 14,   // corner WITHOUT leaving the diagonal; pitch is 127mm, so tight
  DD90R    = 15,
  TURN_COUNT
};

// NOTHING DRIVES ANYTHING PAST SS90R YET. The solver is four-connected -
// Heading has four values and the flood only walks N/E/S/W - so there is no
// diagonal navigation to call these from. They are slots with correct angles
// and reasoned starting geometry, waiting for the path generator in Ch9 and an
// expanded heading set. Do not read their presence as a working feature.
extern TurnParameters turn_params[TURN_COUNT];

// Names for reports, indexed the same way.
extern const char *const turn_names[TURN_COUNT];

#endif // MOUSE_CONFIG_H
