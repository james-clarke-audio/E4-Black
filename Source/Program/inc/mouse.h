/*
 * mouse.h  --  the search/solve brain, ported from mazerunner-core.
 *
 * Adaptations for E4 (STM32, sensor-free bring-up):
 *   - Serial.*        -> report_write / report_printf (Bluetooth)
 *   - millis()/delay  -> HAL_GetTick() / HAL_Delay()
 *   - motion/maze/sensors/switches/reporter are our objects with matching APIs
 *   - walls come from the VIRTUAL sensor (ground-truth maze) refreshed each cell
 *   - no analog front sensor yet: adjustPosition() is a no-op and stopAtCentre()
 *     always coasts to the cell centre (front-wall stop returns with sensors)
 *   - the run lifecycle uses control_run_begin()/_end(); search_to() only
 *     drive-resets so heading/pose stay continuous across a multi-leg search
 *   - pose + discovered walls are streamed to the companion app each step
 *   - the sensor-calibration / SS90 test functions are not ported
 */
#ifndef MOUSE_H
#define MOUSE_H

#include <stdlib.h>          // rand
#include "config.h"          // LED_*, SWITCH_*, HAL_*
#include "mouse_config.h"    // FULL_CELL, SEARCH_*, turn_params, ...
#include "maze.h"            // Maze, Location, Heading, Direction
#include "motion.h"          // motion
#include "reporter.h"        // reporter
#include "robot_sensors.h"   // sensors (virtual), truth
#include "robot_switches.h"  // switches
#include "report.h"          // report_write/printf/pose
#include "control.h"         // control_run_begin/_end, control_pose_reset, control_pose_*
#include "timing.h"          // straight_time / turn_time -- the planner's own model
#include "diagonal.h"        // plan::Route, Robot, DiagTurns, route_times
#include "maze_store.h"     // persist the discovered maze to EEPROM

class Mouse;
extern Mouse mouse;

class Mouse {
 public:
  enum State { FRESH_START, SEARCHING, INPLACE_RUN, SMOOTH_RUN, FINISHED };
  // TurnType now lives in mouse_config.h beside the table it indexes, so the
  // two cannot drift apart. The unqualified names still resolve here.

  Mouse() { init(); }

  void init() {
    m_handStart = false;
    sensors.set_steering_mode(STEERING_OFF);
    m_location = Location(0, 0);
    m_heading = NORTH;
  }

  void set_heading(Heading new_heading) { m_heading = new_heading; }

  //---- app streaming (E4 addition) ----------------------------------------
  void stream_pose() {
    report_pose(control_pose_x(), control_pose_y(), control_pose_heading());
  }
  void stream_cell() {
    WallInfo w = maze.walls(m_location);
    int mask = (w.north == WALL ? 1 : 0) | (w.east == WALL ? 2 : 0) |
               (w.south == WALL ? 4 : 0) | (w.west == WALL ? 8 : 0);
    report_printf("W,%d,%d,%d\r\n", m_location.x, m_location.y, mask);
    report_printf("M,%d,%d,%d\r\n", m_location.x, m_location.y, (int)m_heading);
    stream_pose();
  }

  // stream one arbitrary cell's discovered walls to the app (not just current)
  void stream_named_cell(Location c) {
    WallInfo w = maze.walls(c);
    int mask = (w.north == WALL ? 1 : 0) | (w.east == WALL ? 2 : 0) |
               (w.south == WALL ? 4 : 0) | (w.west == WALL ? 8 : 0);
    report_printf("W,%d,%d,%d\r\n", c.x, c.y, mask);
  }

  //---- centre 2x2 goal room ("one entrance" rule) --------------------------
  // The classic full-size goal is a 2x2 room with exactly ONE entrance. As soon
  // as she steps into it, the whole room is known: the 4 inner walls are open,
  // the 8 perimeter segments are walls EXCEPT the one she came in through - so we
  // don't have to drive through all four squares to map the centre.
  bool goal_room_active() {
    Location g = maze.goal();
    return (g.x == GOAL_ROOM_X0 || g.x == GOAL_ROOM_X0 + 1) &&
           (g.y == GOAL_ROOM_Y0 || g.y == GOAL_ROOM_Y0 + 1);
  }
  bool in_goal_room(Location c) {
    return (c.x == GOAL_ROOM_X0 || c.x == GOAL_ROOM_X0 + 1) &&
           (c.y == GOAL_ROOM_Y0 || c.y == GOAL_ROOM_Y0 + 1);
  }
  void assert_goal_room() {
    const int x0 = GOAL_ROOM_X0, y0 = GOAL_ROOM_Y0;
    Location c00(x0, y0), c10(x0 + 1, y0), c01(x0, y0 + 1), c11(x0 + 1, y0 + 1);
    Heading entrance = behind_from(m_heading);   // the wall she just crossed to enter
    // 4 inner walls open (set_wall keeps both sides consistent)
    maze.set_wall(c00, NORTH, EXIT);
    maze.set_wall(c00, EAST,  EXIT);
    maze.set_wall(c11, SOUTH, EXIT);
    maze.set_wall(c11, WEST,  EXIT);
    // 8 perimeter segments closed
    maze.set_wall(c00, SOUTH, WALL);  maze.set_wall(c00, WEST, WALL);
    maze.set_wall(c10, SOUTH, WALL);  maze.set_wall(c10, EAST, WALL);
    maze.set_wall(c01, NORTH, WALL);  maze.set_wall(c01, WEST, WALL);
    maze.set_wall(c11, NORTH, WALL);  maze.set_wall(c11, EAST, WALL);
    // re-open the single entrance she came through
    maze.set_wall(m_location, entrance, EXIT);
    // tell the app the room is now known (draws red = known)
    stream_named_cell(c00); stream_named_cell(c10);
    stream_named_cell(c01); stream_named_cell(c11);
    report_write("ACT,room\r\n");
  }
  void maybe_assert_goal_room() {
    if (!m_goalRoomAsserted && goal_room_active() && in_goal_room(m_location)) {
      assert_goal_room();
      m_goalRoomAsserted = true;
    }
  }

  //---- in-place turns ------------------------------------------------------
  void turn_IP180() {
    static int direction = 1;
    direction *= -1;  // alternate each call so she does not unwind cables one way
    motion.spin_turn(direction * 180, OMEGA_SPIN_TURN, ALPHA_SPIN_TURN);
  }
  void turn_IP90R() { motion.spin_turn(-90, OMEGA_SPIN_TURN, ALPHA_SPIN_TURN); }
  void turn_IP90L() { motion.spin_turn(90, OMEGA_SPIN_TURN, ALPHA_SPIN_TURN); }

  //---- smooth (curved) search turn ----------------------------------------
  void turn_smooth(int turn_id) {
    sensors.set_steering_mode(STEERING_OFF);
    motion.set_target_velocity(SEARCH_TURN_SPEED);
    TurnParameters params = turn_params[turn_id];

    float trigger = params.trigger;
    if (sensors.see_left_wall)  { trigger += EXTRA_WALL_ADJUST; }
    if (sensors.see_right_wall) { trigger += EXTRA_WALL_ADJUST; }

    bool triggered_by_sensor = false;
    float turn_point = FULL_CELL + HALF_CELL - params.entry_offset;
    while (motion.position() < turn_point) {          // wait for the turn point
      if (sensors.get_front_sum() > trigger) {        // (0 for now -> distance only)
        motion.set_target_velocity(motion.velocity());
        triggered_by_sensor = true;
        break;
      }
      motion.stream_periodic();
    }
    char note = triggered_by_sensor ? 's' : 'd';
    char dir = (turn_id & 1) ? 'R' : 'L';
    reporter.log_action_status(dir, note, m_location, m_heading);
    motion.turn(params.angle, params.omega, 0, params.alpha);   // forward held -> curve
    // E4: our curve ends ~exit_offset mm PAST the new cell's sensing point
    // (mazerunner undershot and drove forward to it). Relabel the frame by that
    // offset so continuing cells stay aligned and the target stop lands centred.
    // SEARCH_SPEED == SEARCH_TURN_SPEED, so no speed-resume move is needed yet.
    motion.set_position(SENSING_POSITION + params.exit_offset);
  }

  //---- execute a planned route ---------------------------------------------
  //
  // The planner says what to do; this does it. One walk serves both the real
  // run and the rehearsal, because the part worth getting right is the
  // SEQUENCING -- which turn, after how many cells, from which heading -- and
  // a simulator that sequenced differently from the driver would prove nothing
  // about the driver.
  //
  // Timing comes from route_times(), which is the same step_time() that costed
  // the route. Two models of one mouse drift, and the drift arrives as a run
  // that took longer than promised for reasons nobody can name.
  //
  // THE SIMULATED TURN IS A SIMPLIFICATION worth naming: position advances
  // during the run and heading rotates at the vertex, so on screen an arc
  // looks like a spin. The TIME is right either way, and the route line drawn
  // over the maze shows the true path -- but do not read the animation as
  // evidence about arc geometry. Zigzag test is what answers that.
  static const int LHX[4], LHY[4], LDX[4], LDY[4];

  static float diag_deg(int dd) {
    switch (dd) { case 1: return -135.0f; case 2: return 135.0f; case 3: return 45.0f; }
    return -45.0f;                                   // NE
  }
  static void collect_times(void *ctx, int i, const plan::Step &, float run_s, float turn_s) {
    Mouse *m = (Mouse *)ctx;
    if (i >= 0 && i < plan::MAX_STEPS) { m->m_run_s[i] = run_s; m->m_turn_s[i] = turn_s; }
  }

  /// The turn-table row a planned move is driven from, and the angle it turns.
  /// Returns -1 for moves that are not table turns (spins, and the goal).
  static int row_of(plan::Move mv, float &angle) {
    switch (mv) {
      case plan::MV_ARC_L:   angle =  90.0f; return SS90L;
      case plan::MV_ARC_R:   angle = -90.0f; return SS90R;
      case plan::MV_ARC_180: angle = 180.0f; return SS180L;
      case plan::MV_SD45_L:  angle =  45.0f; return SD45L;
      case plan::MV_SD45_R:  angle = -45.0f; return SD45R;
      case plan::MV_DS45_L:  angle =  45.0f; return DS45L;
      case plan::MV_DS45_R:  angle = -45.0f; return DS45R;
      case plan::MV_DD90_L:  angle =  90.0f; return DD90L;
      case plan::MV_DD90_R:  angle = -90.0f; return DD90R;
      default: angle = 0.0f; return -1;
    }
  }

  void run_route(const plan::Route &rt, const plan::Robot &rb,
                 const plan::DiagTurns &dt, bool simulate) {
    if (!rt.ok || rt.count <= 0) { report_write("SR,no route\r\n"); return; }

    for (int i = 0; i < plan::MAX_STEPS; i++) { m_run_s[i] = 0.0f; m_turn_s[i] = 0.0f; }
    plan::route_times(rt, rb, dt, plan::NN, &Mouse::collect_times, this);

    int u = 2 * START.x + 1, v = 2 * START.y + 1;
    int h = (int)plan::NN, dd = 0;
    bool on_diag = false;
    float deg = 0.0f;
    m_sim_seconds = 0.0f;
    m_route_off = 0.0f;

    if (!simulate) {
      control_run_begin();
      control_pose_reset();
      motion.move(BACK_WALL_TO_CENTER, RUN_SPEED, RUN_SPEED, RUN_ACCELERATION);
      motion.set_position(HALF_CELL);
    } else {
      control_pose_set(u * HALF_CELL, v * HALF_CELL, 0.0f);
    }

    report_printf("SR,start steps=%d predicted=%dms%s\r\n", rt.count,
                  (int)(rt.seconds * 1000.0f), simulate ? " sim" : "");

    for (int i = 0; i < rt.count; i++) {
      if (switches.button_pressed()) break;
      const plan::Step &st = rt.steps[i];

      // --- the run before the turn ----------------------------------------
      const int u0 = u, v0 = v;
      if (st.diag) { u += st.cells * LDX[dd]; v += st.cells * LDY[dd]; }
      else         { u += st.cells * 2 * LHX[h]; v += st.cells * 2 * LHY[h]; }

      if (simulate) {
        if (st.cells > 0) {
          sim_glide(u0 * HALF_CELL, v0 * HALF_CELL, deg,
                    u * HALF_CELL, v * HALF_CELL, deg, m_run_s[i]);
        }
      } else {
        const float pitch = st.diag ? plan::DIAG_PITCH : FULL_CELL;
        float angle; const int row = row_of(st.move, angle);
        const float exit_speed = (row >= 0) ? (float)turn_params[row].speed : RUN_SPEED;
        const float entry = (row >= 0) ? (float)turn_params[row].entry_offset : 0.0f;
        const float dist = st.cells * pitch - m_route_off - entry;
        if (dist > 1.0f) motion.move(dist, RUN_SPEED, exit_speed, RUN_ACCELERATION);
        m_route_off = entry;
      }

      // --- the turn --------------------------------------------------------
      float angle; const int row = row_of(st.move, angle);
      switch (st.move) {
        case plan::MV_GOAL:
          break;
        case plan::MV_SPIN_L: case plan::MV_SPIN_R: case plan::MV_SPIN_180: {
          const float a = (st.move == plan::MV_SPIN_L) ? 90.0f
                        : (st.move == plan::MV_SPIN_R) ? -90.0f : 180.0f;
          h = (st.move == plan::MV_SPIN_L) ? (h + 3) & 3
            : (st.move == plan::MV_SPIN_R) ? (h + 1) & 3 : (h + 2) & 3;
          if (!simulate) { motion.spin_turn(a, OMEGA_SPIN_TURN, ALPHA_SPIN_TURN); }
          break;
        }
        case plan::MV_ARC_L: case plan::MV_ARC_R: case plan::MV_ARC_180:
          h = (st.move == plan::MV_ARC_L) ? (h + 3) & 3
            : (st.move == plan::MV_ARC_R) ? (h + 1) & 3 : (h + 2) & 3;
          if (!simulate && row >= 0) {
            motion.set_target_velocity((float)turn_params[row].speed);
            motion.turn(angle, turn_params[row].omega, 0.0f, turn_params[row].alpha);
          }
          break;
        case plan::MV_SD45_L: case plan::MV_SD45_R: {
          const int nd = (st.move == plan::MV_SD45_L) ? ((h + 3) & 3) : h;
          u += LDX[nd] - LHX[h];
          v += LDY[nd] - LHY[h];
          dd = nd; on_diag = true;
          if (!simulate && row >= 0) {
            motion.set_target_velocity((float)turn_params[row].speed);
            motion.turn(angle, turn_params[row].omega, 0.0f, turn_params[row].alpha);
          }
          break;
        }
        case plan::MV_DS45_L: case plan::MV_DS45_R: {
          const int nh = (st.move == plan::MV_DS45_L) ? ((dd + 3) & 3) : ((dd + 1) & 3);
          u += LHX[nh]; v += LHY[nh];
          h = nh; on_diag = false;
          if (!simulate && row >= 0) {
            motion.set_target_velocity((float)turn_params[row].speed);
            motion.turn(angle, turn_params[row].omega, 0.0f, turn_params[row].alpha);
          }
          break;
        }
        case plan::MV_DD90_L: case plan::MV_DD90_R:
          dd = (st.move == plan::MV_DD90_L) ? (dd + 3) & 3 : (dd + 1) & 3;
          if (!simulate && row >= 0) {
            motion.set_target_velocity((float)turn_params[row].speed);
            motion.turn(angle, turn_params[row].omega, 0.0f, turn_params[row].alpha);
          }
          break;
        default: break;
      }

      const float nd_deg = on_diag ? diag_deg(dd) : heading_deg((Heading)h);
      if (simulate && m_turn_s[i] > 0.0001f) {
        sim_glide(u * HALF_CELL, v * HALF_CELL, deg,
                  u * HALF_CELL, v * HALF_CELL, nd_deg, m_turn_s[i]);
      }
      deg = nd_deg;

      if (st.move == plan::MV_GOAL) break;
    }

    if (!simulate) { motion.reset_drive_system(); control_run_end(); }
    else { control_pose_set(u * HALF_CELL, v * HALF_CELL, deg); control_stream_telemetry(); }

    m_location = Location((uint8_t)((u - 1) / 2), (uint8_t)((v - 1) / 2));
    m_heading  = (Heading)h;
    report_printf("SR,done at (%d,%d) predicted=%dms model=%dms\r\n",
                  m_location.x, m_location.y, (int)(rt.seconds * 1000.0f),
                  (int)(m_sim_seconds * 1000.0f));
  }

  //---- stop at the centre of the current cell ------------------------------
  // No analog front sensor yet, so we always coast to the profiled centre.
  void stopAtCentre() {
    sensors.set_steering_mode(STEERING_OFF);
    float remaining = (FULL_CELL + HALF_CELL) - motion.position();
    motion.start_move(remaining, motion.velocity(), 0, motion.acceleration());
    uint32_t timeout = HAL_GetTick() + 1000;
    while (!motion.move_finished()) {
      if (HAL_GetTick() > timeout) { break; }
      motion.stream_periodic();
      HAL_Delay(2);
    }
    motion.reset_drive_system();
  }

  // Front-wall fine-tune: a no-op until the analog front sensor exists.
  void adjustPosition() { return; }

  //---- one-cell moves ------------------------------------------------------
  void move_ahead() {
    motion.adjust_forward_position(-FULL_CELL);
    motion.wait_until_position(SENSING_POSITION);
  }
  void turn_left()  { turn_smooth(SS90EL); m_heading = left_from(m_heading); }
  void turn_right() { turn_smooth(SS90ER); m_heading = right_from(m_heading); }

  void turn_back() {
    reporter.log_action_status('B', ' ', m_location, m_heading);
    stopAtCentre();
    adjustPosition();
    turn_IP180();
    float distance = SENSING_POSITION - HALF_CELL;
    motion.move(distance, SEARCH_SPEED, SEARCH_SPEED, SEARCH_ACCELERATION);
    motion.set_position(SENSING_POSITION);
    m_heading = behind_from(m_heading);
  }

  //---- turn on the spot to face a heading ----------------------------------
  void turn_to_face(Heading newHeading) {
    unsigned char hdgChange = (newHeading + HEADING_COUNT - m_heading) % HEADING_COUNT;
    switch (hdgChange) {
      case AHEAD: break;
      case RIGHT: turn_IP90R(); break;
      case BACK:  turn_IP180(); break;
      case LEFT:  turn_IP90L(); break;
    }
    m_heading = newHeading;
  }

  //---- record the walls of the current cell into the map -------------------
  void update_map() {
    bool leftWall  = sensors.see_left_wall;
    bool frontWall = sensors.see_front_wall;
    bool rightWall = sensors.see_right_wall;
    switch (m_heading) {
      case NORTH:
        maze.update_wall_state(m_location, NORTH, frontWall ? WALL : EXIT);
        maze.update_wall_state(m_location, EAST, rightWall ? WALL : EXIT);
        maze.update_wall_state(m_location, WEST, leftWall ? WALL : EXIT);
        break;
      case EAST:
        maze.update_wall_state(m_location, EAST, frontWall ? WALL : EXIT);
        maze.update_wall_state(m_location, SOUTH, rightWall ? WALL : EXIT);
        maze.update_wall_state(m_location, NORTH, leftWall ? WALL : EXIT);
        break;
      case SOUTH:
        maze.update_wall_state(m_location, SOUTH, frontWall ? WALL : EXIT);
        maze.update_wall_state(m_location, WEST, rightWall ? WALL : EXIT);
        maze.update_wall_state(m_location, EAST, leftWall ? WALL : EXIT);
        break;
      case WEST:
        maze.update_wall_state(m_location, WEST, frontWall ? WALL : EXIT);
        maze.update_wall_state(m_location, NORTH, rightWall ? WALL : EXIT);
        maze.update_wall_state(m_location, SOUTH, leftWall ? WALL : EXIT);
        break;
      default:
        break;
    }
    stream_cell();   // E4: push discovered walls + pose to the app
  }

  //---- search to a target cell, mapping as we go ---------------------------
  void search_to(Location target) {
    maze.flood(target);
    if (maze.flood_queue_overflow()) { reporter.log_action_status('*', ' ', m_location, m_heading); panic(); return; }

    HAL_Delay(200);
    motion.reset_drive_system();   // clean slate; run + heading stay live
    sensors.set_steering_mode(STEERING_OFF);
    if (not m_handStart) {
      motion.move(-BACK_WALL_TO_CENTER, SEARCH_SPEED / 4, 0, SEARCH_ACCELERATION / 2);
    }
    motion.move(BACK_WALL_TO_CENTER, SEARCH_SPEED, SEARCH_SPEED, SEARCH_ACCELERATION);
    motion.set_position(HALF_CELL);
    motion.wait_until_position(SENSING_POSITION);

    // each iteration starts at the sensing point of the current cell
    while (m_location != target) {
      if (switches.button_pressed()) { break; }
      reporter.log_action_status('-', ' ', m_location, m_heading);
      sensors.set_steering_mode(STEER_NORMAL);
      m_location = m_location.neighbour(m_heading);   // the cell we are entering
      sensors.update(m_location, m_heading);          // E4: refresh virtual walls
      update_map();
      maybe_assert_goal_room();                       // fill the centre room on first entry
      if (m_location != target) {                     // only decide a move if not yet arrived
        maze.flood(target);
        if (maze.flood_queue_overflow()) { reporter.log_action_status('*', ' ', m_location, m_heading); panic(); return; }
        unsigned char newHeading = maze.heading_to_smallest(m_location, m_heading);
        if (newHeading == BLOCKED) { report_write("ERR: no route to target\r\n"); panic(); return; }
        unsigned char hdgChange = (newHeading - m_heading) & 0x3;
        switch (hdgChange) {
          case AHEAD: move_ahead(); break;
          case RIGHT: turn_right(); break;
          case BACK:  turn_back();  break;
          case LEFT:  turn_left();  break;
        }
      }
    }
    stopAtCentre();
    adjustPosition();
    stream_cell();
    report_write("Arrived!\r\n");
    HAL_Delay(250);
    motion.reset_drive_system();
    sensors.set_steering_mode(STEERING_OFF);
  }

  //---- a simple left-wall follower that knows where it is -------------------
  //---- wall follower -------------------------------------------------------
  // Keep one hand on the wall and walk. No map, no memory, no flood.
  //
  // This is NOT a fallback solver, it is an entrant in a DIFFERENT EVENT.
  // Wall-follower courses are built with a wall connected all the way to the
  // centre, so a follower always arrives -- that is the point of the class.
  // Maze-solver mazes are built the other way round, with the outside
  // deliberately disconnected from the inside, so a follower can never reach
  // the middle of one however long it walks.
  //
  // So the step limit below is not there because the algorithm is weak. It is
  // there because this same code will be pointed at a solver maze in the sim,
  // where not arriving is the CORRECT answer and the only wrong behaviour
  // would be walking for ever while it is true.
  //
  // She also MAPS as she follows, so even a lap that never finds a centre
  // leaves those walls in the map.
  void follow_to(Location target, bool right_hand) {
    // ARM FIRST. Every other action that turns a wheel waits for a press on
    // HER before it moves, and this one did not -- so firing it from the app
    // put her in motion the instant the button was tapped, with nobody
    // necessarily near her. The simulated twin waits too, so that both halves
    // of the pair behave the same way and neither teaches you a habit the
    // other punishes.
    sensors.wait_for_user_start();
    m_handStart = true;
    m_location = START;
    m_heading = NORTH;
    maze.initialise();
    control_run_begin();
    control_pose_reset();
    report_write("RST\r\n");
    report_known_map();          // the perimeter, before she has seen a thing
    sensors.set_steering_mode(STEERING_OFF);
    motion.move(BACK_WALL_TO_CENTER, SEARCH_SPEED, SEARCH_SPEED, SEARCH_ACCELERATION);
    motion.set_position(HALF_CELL);
    motion.wait_until_position(SENSING_POSITION);
    int steps = 0;
    while (m_location != target) {
      if (switches.button_pressed()) { break; }
      sensors.set_steering_mode(STEER_NORMAL);
      m_location = m_location.neighbour(m_heading);
      sensors.update(m_location, m_heading);
      update_map();
      if (m_location != target) {
        if (right_hand) {
          if      (!sensors.see_right_wall) { turn_right(); }
          else if (!sensors.see_front_wall) { move_ahead(); }
          else if (!sensors.see_left_wall)  { turn_left();  }
          else                              { turn_back();  }
        } else {
          if      (!sensors.see_left_wall)  { turn_left();  }
          else if (!sensors.see_front_wall) { move_ahead(); }
          else if (!sensors.see_right_wall) { turn_right(); }
          else                              { turn_back();  }
        }
      }
      if (++steps > FOLLOW_STEP_LIMIT) {
        report_printf("WF,gave up after %d cells (%s hand)\r\n",
                      steps, right_hand ? "right" : "left");
        break;
      }
    }
    stopAtCentre();
    adjustPosition();
    stream_cell();
    report_printf("WF,done %s hand at (%d,%d) %d cells\r\n",
                  right_hand ? "right" : "left", m_location.x, m_location.y, steps);
    HAL_Delay(250);
    control_run_end();
  }

  //---- random-walk explore (novelty / fallback) ----------------------------
  bool getRandomBool() { return rand() % 2 == 0; }
  uint8_t randomHeading() {
    bool L = sensors.see_left_wall, R = sensors.see_right_wall, F = sensors.see_front_wall;
    if (L && R && F) return BACK;
    if (L && R)      return AHEAD;
    if (R && F)      return LEFT;
    if (L && F)      return RIGHT;
    if (L)           return getRandomBool() ? RIGHT : AHEAD;
    if (R)           return getRandomBool() ? LEFT : AHEAD;
    return getRandomBool() ? LEFT : RIGHT;
  }

  //---- report the solved route + timing back to the app -----------------
  // Walks the flood (goal-relative, from the last search flood) start->goal
  // to recover the route she found, and reports how long the run took.
  void report_solution(uint32_t ms) {
    report_write("SOLVE\r\n");
    Location c = START;
    int steps = 0, guard = 0;
    while (c != maze.goal() && guard++ < 400) {
      report_printf("SP,%d,%d\r\n", c.x, c.y);
      Heading h = maze.heading_to_smallest(c, NORTH);
      if (h == BLOCKED) break;
      c = c.neighbour(h);
      steps++;
    }
    report_printf("SP,%d,%d\r\n", maze.goal().x, maze.goal().y);
    report_printf("SOLVED,%lu,%d\r\n", (unsigned long)ms, steps);
  }

  //---- report the OPTIMAL known route (closed-mask) ---------------------
  // Flood with unknowns treated as WALLS (MASK_CLOSED) so the reported path
  // never crosses an unseen wall - a true shortest route over what she knows,
  // not her outbound belief. Used after a full explore (to-goal + return).
  void report_best_route(uint32_t ms) {
    MazeMask save = maze.get_mask();
    maze.set_mask(MASK_CLOSED);
    maze.flood(maze.goal());
    report_write("SOLVE\r\n");
    Location c = START;
    int steps = 0, guard = 0;
    while (c != maze.goal() && guard++ < 400) {
      report_printf("SP,%d,%d\r\n", c.x, c.y);
      Heading h = maze.heading_to_smallest(c, NORTH);
      if (h == BLOCKED) break;
      c = c.neighbour(h);
      steps++;
    }
    report_printf("SP,%d,%d\r\n", maze.goal().x, maze.goal().y);
    report_printf("SOLVED,%lu,%d\r\n", (unsigned long)ms, steps);
    maze.set_mask(save);
  }

  //---- SIMULATE: solve the maze with NO motors, streaming the virtual run --
  // Same brain (flood/decide/map) as the real search, but each decision
  // animates the pose to the next cell instead of driving the wheels - so you
  // can watch her solve any maze on screen without a physical one.
  static float heading_deg(Heading h) {
    switch (h) { case EAST: return -90.0f; case SOUTH: return 180.0f; case WEST: return 90.0f; default: return 0.0f; }
  }
  // How long the move she is about to animate would actually take.
  //
  // AHEAD is one cell at search speed. A 90 is an ARC: she rotates while she
  // travels, so the two overlap and the cost is whichever of them takes
  // longer, not the sum. BACK is the one move that does NOT overlap, because
  // turn_back() stops dead, spins on the spot and moves off again -- so that
  // one adds up. Getting this wrong in the other direction is how a sim ends
  // up flattering a route full of dead ends.
  float sim_move_seconds(int hdg_change) const {
    const float straight = FULL_CELL / SEARCH_SPEED;
    if (hdg_change == AHEAD) return straight;
    if (hdg_change == BACK) {
      return straight + timing::spin_time(180.0f, OMEGA_SPIN_TURN, ALPHA_SPIN_TURN);
    }
    const TurnParameters &p = turn_params[(hdg_change == RIGHT) ? SS90ER : SS90EL];
    const float arc = timing::turn_time(90.0f, p.omega, p.alpha);
    return (arc > straight) ? arc : straight;
  }

  // One animated move, taking as long on screen as it would on the floor.
  //
  // Frames follow the DURATION rather than a fixed count, so a six-cell
  // straight takes six times the frames of one cell and a dead-end reversal
  // visibly costs what it costs. The frame deadline ABSORBS the time spent
  // transmitting -- a POS and a TEL pair is about 56 bytes, roughly 10 ms of
  // wire at 57600 -- because adding the link's latency on top of the frame
  // would make every sim run a third slower than the model it is showing, and
  // the whole point is that the two agree.
  static const uint32_t SIM_FRAME_MS = 30;

  // The animator, in millimetres. sim_step() is the cell-to-cell caller; the
  // route executor works in half-cells and calls this directly, because a
  // diagonal leg does not begin or end at a cell centre.
  void sim_glide(float ax, float ay, float da, float bx, float by, float db, float seconds) {
    float dd = db - da; while (dd > 180.0f) dd -= 360.0f; while (dd < -180.0f) dd += 360.0f;

    m_sim_seconds += seconds;

    const float rate = (SIM_RATE > 0.05f) ? SIM_RATE : 1.0f;
    int NS = (int)((seconds * 1000.0f / rate) / (float)SIM_FRAME_MS + 0.5f);
    if (NS < 1) NS = 1;
    if (NS > 240) NS = 240;

    uint32_t due = HAL_GetTick();
    for (int i = 1; i <= NS; i++) {
      if (switches.button_pressed()) return;
      float t = (float)i / NS;
      control_pose_set(ax + (bx - ax) * t, ay + (by - ay) * t, da + dd * t);
      control_stream_telemetry();
      due += SIM_FRAME_MS;
      while ((int32_t)(HAL_GetTick() - due) < 0) { }
    }
  }

  void sim_step(Location a, Location b, float da, float db, float seconds) {
    float ax = a.x * FULL_CELL + HALF_CELL, ay = a.y * FULL_CELL + HALF_CELL;
    float bx = b.x * FULL_CELL + HALF_CELL, by = b.y * FULL_CELL + HALF_CELL;
    float dd = db - da; while (dd > 180.0f) dd -= 360.0f; while (dd < -180.0f) dd += 360.0f;

    m_sim_seconds += seconds;            // the modelled clock, not the wall one

    const float rate = (SIM_RATE > 0.05f) ? SIM_RATE : 1.0f;
    int NS = (int)((seconds * 1000.0f / rate) / (float)SIM_FRAME_MS + 0.5f);
    if (NS < 1) NS = 1;
    if (NS > 240) NS = 240;              // nothing legitimate is 7 s in one cell

    uint32_t due = HAL_GetTick();
    for (int i = 1; i <= NS; i++) {
      if (switches.button_pressed()) return;
      float t = (float)i / NS;
      control_pose_set(ax + (bx - ax) * t, ay + (by - ay) * t, da + dd * t);
      control_stream_telemetry();
      due += SIM_FRAME_MS;
      while ((int32_t)(HAL_GetTick() - due) < 0) { }
    }
  }
  // sim: flood/decide/animate to `target`, mapping + asserting the centre room.
  // Assumes m_location/m_heading are set and the initial pose is placed.
  void sim_search_to(Location target) {
    sensors.update(m_location, m_heading);
    update_map();
    maybe_assert_goal_room();
    maze.flood(target);
    int guard = 0;
    while (m_location != target && guard++ < 1000) {
      if (switches.button_pressed()) break;
      Heading nh = maze.heading_to_smallest(m_location, m_heading);
      if (nh == BLOCKED) { report_write("ERR: no route to target\r\n"); break; }
      Location next = m_location.neighbour(nh);
      sim_step(m_location, next, heading_deg(m_heading), heading_deg(nh),
               sim_move_seconds(((int)nh - (int)m_heading) & 3));
      m_location = next;
      m_heading = nh;
      sensors.update(m_location, m_heading);
      update_map();
      maybe_assert_goal_room();
      maze.flood(target);
    }
    control_pose_set(m_location.x * FULL_CELL + HALF_CELL, m_location.y * FULL_CELL + HALF_CELL, heading_deg(m_heading));
    control_stream_telemetry();
  }

  // sim: the wall follower, animated, motors never armed. Same decision, same
  // map updates; only the driving is replaced. The step limit matters more here
  // than on the floor, because nothing runs out of battery to stop her.
  void sim_follow_to(Location target, bool right_hand) {
    sensors.update(m_location, m_heading);
    update_map();
    maybe_assert_goal_room();
    int steps = 0;
    bool gave_up = false;
    while (m_location != target) {
      if (switches.button_pressed()) break;
      if (++steps > FOLLOW_STEP_LIMIT) { gave_up = true; break; }
      Heading nh;
      if (right_hand) {
        if      (!sensors.see_right_wall) nh = right_from(m_heading);
        else if (!sensors.see_front_wall) nh = m_heading;
        else if (!sensors.see_left_wall)  nh = left_from(m_heading);
        else                              nh = behind_from(m_heading);
      } else {
        if      (!sensors.see_left_wall)  nh = left_from(m_heading);
        else if (!sensors.see_front_wall) nh = m_heading;
        else if (!sensors.see_right_wall) nh = right_from(m_heading);
        else                              nh = behind_from(m_heading);
      }
      Location next = m_location.neighbour(nh);
      sim_step(m_location, next, heading_deg(m_heading), heading_deg(nh),
               sim_move_seconds(((int)nh - (int)m_heading) & 3));
      m_location = next;
      m_heading = nh;
      sensors.update(m_location, m_heading);
      update_map();
      maybe_assert_goal_room();
    }
    control_pose_set(m_location.x * FULL_CELL + HALF_CELL,
                     m_location.y * FULL_CELL + HALF_CELL, heading_deg(m_heading));
    control_stream_telemetry();
    if (gave_up) {
      report_printf("WF,gave up after %d cells (%s hand)\r\n", steps,
                    right_hand ? "right" : "left");
    }
    report_printf("WF,done %s hand at (%d,%d) %d cells %dms\r\n",
                  right_hand ? "right" : "left", m_location.x, m_location.y,
                  steps, (int)(m_sim_seconds * 1000.0f));
  }

  // sim: wall follow from the start cell to the goal, with the map redrawn.
  void simulate_follow(bool right_hand) {
    sensors.wait_for_user_start();
    m_handStart = true;
    m_location = START;
    m_heading = NORTH;
    m_goalRoomAsserted = false;
    maze.initialise();
    control_pose_set(START.x * FULL_CELL + HALF_CELL, START.y * FULL_CELL + HALF_CELL, 0.0f);
    report_write("RST\r\n");
    report_printf("GOAL,%d,%d\r\n", maze.goal().x, maze.goal().y);
    report_known_map();          // the perimeter, before she has seen a thing
    report_write("STATE,SIM\r\n");
    m_sim_seconds = 0.0f;
    sim_follow_to(maze.goal(), right_hand);
    report_write("STATE,IDLE\r\n");
  }

  void simulate_to_goal() {
    sensors.wait_for_user_start();
    m_handStart = true;
    m_location = START;
    m_heading = NORTH;
    m_goalRoomAsserted = false;
    maze.initialise();
    control_pose_set(START.x * FULL_CELL + HALF_CELL, START.y * FULL_CELL + HALF_CELL, 0.0f);
    report_write("RST\r\n");
    report_printf("GOAL,%d,%d\r\n", maze.goal().x, maze.goal().y);
    report_known_map();          // the perimeter, before she has seen a thing
    report_write("STATE,SIM\r\n");
    // The MODELLED clock, not HAL_GetTick(). They now agree to within the odd
    // frame -- which is exactly why the wall clock must not be the one
    // reported: a slow link or a stalled frame would then read as a slower
    // mouse, and the figure would stop being a property of the route.
    m_sim_seconds = 0.0f;
    sim_search_to(maze.goal());
    report_solution((uint32_t)(m_sim_seconds * 1000.0f));
    report_write("STATE,IDLE\r\n");
  }

  // sim: full explore -- to the goal, then back to the start, then report the
  // optimal (closed-mask) route. Motor-free; watch the whole thing on screen.
  void simulate_explore() {
    sensors.wait_for_user_start();
    m_handStart = true;
    m_location = START;
    m_heading = NORTH;
    m_goalRoomAsserted = false;
    maze.initialise();
    control_pose_set(START.x * FULL_CELL + HALF_CELL, START.y * FULL_CELL + HALF_CELL, 0.0f);
    report_write("RST\r\n");
    report_printf("GOAL,%d,%d\r\n", maze.goal().x, maze.goal().y);
    report_known_map();          // the perimeter, before she has seen a thing
    report_write("STATE,SIM\r\n");
    m_sim_seconds = 0.0f;                     // modelled seconds, not wall clock
    sim_search_to(maze.goal());
    report_write("STATE,RETURN\r\n");
    sim_search_to(START);
    report_best_route((uint32_t)(m_sim_seconds * 1000.0f));
    report_write(maze_store_save() ? "ACT,saved\r\n" : "ACT,save-fail\r\n");  // persist (real EEPROM, no motors needed)
    report_write("STATE,IDLE\r\n");
  }

  //---- gentle bring-up: search out to the goal and stop (no return) --------
  void search_to_goal() {
    sensors.wait_for_user_start();
    m_handStart = true;
    m_location = START;
    m_heading = NORTH;
    m_goalRoomAsserted = false;
    maze.initialise();
    control_run_begin();
    control_pose_reset();
    uint32_t t0 = HAL_GetTick();
    report_write("RST\r\n");
    report_printf("GOAL,%d,%d\r\n", maze.goal().x, maze.goal().y);
    report_known_map();          // the perimeter, before she has seen a thing
    report_write("STATE,SEARCH\r\n");
    search_to(maze.goal());
    report_solution(HAL_GetTick() - t0);   // route + run time back to the app
    report_write("STATE,IDLE\r\n");
    control_run_end();
  }

  //---- the top-level searcher: to goal, then back to start -----------------
  int search_maze() {
    sensors.wait_for_user_start();
    m_handStart = true;
    m_location = START;
    m_heading = NORTH;
    m_goalRoomAsserted = false;
    maze.initialise();
    control_run_begin();      // enter the run: closed loop + zero heading
    control_pose_reset();     // app pose starts at the start cell
    uint32_t t0 = HAL_GetTick();
    report_write("RST\r\n");
    report_printf("GOAL,%d,%d\r\n", maze.goal().x, maze.goal().y);
    report_known_map();          // the perimeter, before she has seen a thing
    report_write("STATE,SEARCH\r\n");

    search_to(maze.goal());

    maze.flood(START);
    if (maze.flood_queue_overflow()) { reporter.log_action_status('*', ' ', m_location, m_heading); panic(); return 0; }
    Heading best_direction = maze.heading_to_smallest(m_location, m_heading);
    if (best_direction == BLOCKED) { report_write("ERR: no route to start\r\n"); panic(); return 0; }
    report_write("STATE,RETURN\r\n");
    turn_to_face(best_direction);
    m_handStart = false;
    search_to(START);
    turn_to_face(NORTH);
    report_best_route(HAL_GetTick() - t0);   // optimal known route back to the app
    report_write(maze_store_save() ? "ACT,saved\r\n" : "ACT,save-fail\r\n");  // persist to EEPROM
    report_write("STATE,IDLE\r\n");
    control_run_end();
    return 0;
  }

  //---- stream the map she is holding to the app ----------------------------
  // maze.initialise() gives her the perimeter and the start cell's east wall
  // before a single sensor reading -- she does not discover the outside of the
  // arena, she is born knowing it. The app, though, starts from RST with
  // nothing and only learns a wall when a W line arrives, and W lines are only
  // sent for cells she actually stands in. So the border of the maze never
  // reached the app at all: it was known at both ends of the link and sent
  // over neither.
  //
  // Called straight after RST by everything that begins from a fresh map, so
  // what the app draws is what she believes, from the first frame rather than
  // from the first cell she visits.
  void report_known_map() {
    for (int x = 0; x < maze.width(); x++)
      for (int y = 0; y < maze.height(); y++) {
        WallInfo w = maze.walls(Location((uint8_t)x, (uint8_t)y));
        int m = (w.north == WALL ? 1 : 0) | (w.east == WALL ? 2 : 0) |
                (w.south == WALL ? 4 : 0) | (w.west == WALL ? 8 : 0);
        if (m) report_printf("W,%d,%d,%d\r\n", x, y, m);
      }
  }

  //---- recall the last explored maze from EEPROM (no motors) ----------------
  // Loads the saved map, redraws it in the app (known walls) and reports the
  // optimal route - proving the maze survived power-off without re-exploring.
  void recall_from_eeprom() {
    if (!maze_store_load()) { report_write("ACT,no-save\r\n"); return; }
    report_write("RST\r\n");
    report_printf("GOAL,%d,%d\r\n", maze.goal().x, maze.goal().y);
    report_write("STATE,RECALL\r\n");
    report_known_map();
    report_best_route(0);   // optimal route from the remembered map
    report_write("STATE,IDLE\r\n");
  }

  //---- redraw the current arena (bounds + goal) to the app, no motors -------
  // Applies the perimeter for the current bounds, then streams the empty arena
  // and its goal so you can see the test maze before running in it.
  void show_arena() {
    maze.initialise();                 // perimeter for current bounds + reflood
    report_write("RST\r\n");
    report_printf("GOAL,%d,%d\r\n", maze.goal().x, maze.goal().y);
    report_printf("SIZE,%d,%d\r\n", maze.width(), maze.height());
    report_known_map();
    report_write("STATE,IDLE\r\n");
  }

  //---- bring the robot to a safe halt and wait for a button ----------------
  void blink(int count) {
    for (int i = 0; i < count; i++) { LED_ALL_ON(); HAL_Delay(100); LED_ALL_OFF(); HAL_Delay(100); }
  }
  void panic() {
    motion.emergency_stop();
    sensors.set_steering_mode(STEERING_OFF);
    report_write("PANIC\r\n");
    while (!switches.button_pressed()) { blink(1); }
    switches.wait_for_button_release();
  }

 private:
  Heading m_heading;
  Location m_location;
  bool m_handStart = false;
  bool m_goalRoomAsserted = false;

  // Seconds of MODELLED time in the current simulated run. Reported instead of
  // wall clock, so a slow link or a dropped frame shows up as a stutter on
  // screen and never as a slower mouse.
  float m_sim_seconds = 0.0f;

  // Per-step durations for the route being executed, filled by route_times().
  float m_run_s[plan::MAX_STEPS];
  float m_turn_s[plan::MAX_STEPS];
  // How far into the current cell the last turn left her, driven runs only.
  float m_route_off = 0.0f;
};

#endif  // MOUSE_H
