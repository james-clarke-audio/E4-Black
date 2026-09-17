#include <config.h>
#include "app_main.h"
#include "version.h"   // FW_VERSION / FW_BUILD
#include "ssd1306.h"
#include "mpu9250.h"
#include "IRS.h"
#include "PWM.h"
#include "encoders.h"
#include "maze.h"        // Phase 1: ported flood-fill maze solver
#include "native.h"      // time-weighted route planner (diagonal lattice)
#include "profile.h"     // Phase 1: ported trapezoidal motion profiles
#include "report.h"      // Phase 1: UART maze/telemetry reporting
#include "bluetooth.h"   // HM-10/HM-18 baud sweep + AT utility
#include "control.h"     // Phase 2: 1 kHz motion control loop
#include "odometry.h"    // Phase 2: encoder odometry
#include "motors_ctrl.h" // Phase 2: FF + PD motor controller
#include "gyro.h"         // Phase 4: MPU-9250 yaw heading
#include "mouse_config.h" // maze/turn geometry + search tuning
#include "motion.h"      // Motion facade (persistent-run locomotion)
#include "mouse.h"       // Phase: search/solve brain (+ world stubs)
#include "bt_rx.h"       // interrupt-driven UART1 receive ring buffer
#include "eeprom.h"      // 24LC256 persistence driver (diagnostic)
#include "config_store.h" // versioned settings block (gyro scale + wall thresholds)
#include <string.h>
#include <stdlib.h>

// access to global variables in system code generated files
extern I2C_HandleTypeDef hi2c1;
extern SPI_HandleTypeDef hspi2;
extern UART_HandleTypeDef huart1;
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;

// globzl variables
char zz[30];
int16_t cntr = 500;
uint8_t button_held = 0;
int16_t accData[3], gyroData[3];
static uint8_t s_haveOled = 1;   // set from SSD1306_Init(); 0 => run headless (no OLED)

// Two-colour 128x64 SSD1306 on E4: rows 0..15 are YELLOW, rows 16..63 are
// BLUE, with a single non-displayed pixel line at the seam (~y16). Never let
// a text line straddle the seam or it falls in the dead row. Title -> yellow,
// list -> blue. Tune here if a different panel is fitted.
static const int OLED_TITLE_Y = 3;    // title baseline, inside the yellow band
static const int OLED_LIST_Y0 = 18;   // first list row, just below the seam (blue)
static const int OLED_ROW_H   = 12;   // line pitch in the blue band (3 rows: 18,30,42)

float xval,yval,zval;
int16_t xval_raw = 0;
int16_t yval_raw = 0;
int16_t zval_raw = 0;

uint32_t then = 0, now = 0;
uint32_t butthen = 0, butnow = 0;
uint16_t cntL = 0, cntR = 0;

// ===================== Menu / launcher =====================
// Two-button scroll & select: LEFT = next item, RIGHT = run highlighted.
// Also driveable over BT: '1'..'9' run item N, 'n'/'p' navigate, 'r'/space run.
typedef void (*menu_action_t)(void);

static void act_forward (void) { control_forward_move(180.0f, 180.0f, 0.0f, 1000.0f); }
// Both read the ONE turn table, so "Right 90" from the menu and the turn she
// makes while searching are the same turn. They were not before: this used to
// hardcode alpha 1000 while turn_params carried 2500.
static void run_arc_from_table(int idx) {
	const TurnParameters &p = turn_params[idx];
	control_arc_turn((float)p.speed, (float)p.entry_offset, p.angle,
	                 p.omega, p.alpha, (float)p.lead_out);
}
static void act_right90 (void) { run_arc_from_table(1); }   // SS90ER
static void act_left90  (void) { run_arc_from_table(0); }   // SS90EL
// Same divergence the 90s had: this used to hardcode omega 180 / alpha 1000
// while turn_IP180() used 360 / 3600, so "Spin 180" from the menu and the
// 180 she makes at a dead end were different spins.
static void act_spin180 (void) { control_spin(180.0f, OMEGA_SPIN_TURN, 0.0f, ALPHA_SPIN_TURN); }

static void act_recal_gyro(void) {
	SSD1306_Fill(SSD1306_COLOR_BLACK);
	SSD1306_GotoXY(0, 3);  SSD1306_Puts("Recal gyro",    &Font_7x10, SSD1306_COLOR_WHITE);
	SSD1306_GotoXY(0, 26); SSD1306_Puts("hold still...", &Font_7x10, SSD1306_COLOR_WHITE);
	SSD1306_UpdateScreen();
	control_recalibrate_gyro();   // ISR-safe bias re-cal
}
static void act_reset_pose(void) {
	control_pose_reset();
	report_write("RST\r\n");          // tell the app to re-centre
	control_stream_telemetry();
}
static void act_toggle_test(void) {
	control_set_test_mode(!control_test_mode());
	control_pose_reset();             // move the start immediately
	report_write("RST\r\n");
	control_stream_telemetry();
}
static void act_set_bt_57k(void) {
	// One-time: move the MCU<->module UART link to 57600 (module NVM persists).
	// Run with NO phone connected so the module is in AT command mode. The MCU
	// now boots at 57600 (usart.c), so on the first run the module is still at
	// its old baud; ensure_baud sweeps to find it, switches it, and settles here.
	bt_ensure_baud(57600);
	bt_rx_init();      // DeInit/Init during re-baud cleared the RXNE interrupt
	HAL_Delay(1800);   // hold the OLED result so it can be read
}

static void act_bt_provision(void) {
	// Bring-up for a NEW or replacement module, straight out of its bag: find
	// it at whatever baud it ships on, move it to 57600, name it MMOUSE, reset
	// so that name is what it advertises. Takes no input -- the name is fixed.
	//
	// Run with NO phone connected. The module only accepts AT commands while
	// no central is attached, so there is nothing listening over BT either:
	// the OLED is the whole interface for this one.
	bt_provision();
	bt_rx_init();      // re-baud cleared the RXNE interrupt
	HAL_Delay(2600);   // hold the result -- there are four lines to read
}

// Substrate self-test: prove the persistent-run + concurrent forward/rotation
// model makes a coordinated curve. Scripts fwd -> smooth right -> fwd via the
// Motion facade. (Temporary home on menu item 8 until the brain port lands.)
static void act_motion_test(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }  // wait for release
	HAL_Delay(800);                                            // hands-off settle
	report_write("motion test: fwd, smooth R, fwd\r\n");
	control_run_begin();                                       // closed loop live for the whole run
	motion.move(BACK_WALL_TO_CENTER, SEARCH_SPEED, SEARCH_TURN_SPEED, SEARCH_ACCELERATION);
	motion.set_position(HALF_CELL);
	motion.move(FULL_CELL, SEARCH_TURN_SPEED, SEARCH_TURN_SPEED, SEARCH_ACCELERATION);
	motion.set_target_velocity(SEARCH_TURN_SPEED);             // hold speed through the turn
	motion.turn(-90.0f, 170.0f, 0.0f, 1000.0f);                // rotation + forward together = curve
	motion.move(FULL_CELL, SEARCH_TURN_SPEED, 0.0f, SEARCH_ACCELERATION);
	control_run_end();
	report_printf("motion test done: gyro_a=%d d=%d\r\n",
	              (int)gyro.angle(), (int)odometry.robot_distance());
}

// --- Chained-zigzag test -------------------------------------------------
//
// SETTLES AN ARGUMENT between the route planner and the firmware.
//
// A chainable 90 is a quarter circle of radius 90 mm -- HALF A CELL. It starts
// at the midpoint of the wall she enters by and ends at the midpoint of the
// wall she leaves by, so it begins and ends exactly on a cell boundary, on the
// corridor centreline, and the next one can start where the last finished with
// nothing in between. 90 mm forward, 90 mm sideways. That is the whole
// requirement, and it is geometry, not tuning.
//
// She does not do that. R = v / omega = 300 / (170 deg/s in rad) = 101 mm is
// the number usually quoted, and it is already too wide -- but it is also not
// the displacement, because alpha is finite and omega ramps. Integrating the
// actual trapezoidal profile at v 300, omega 170, alpha 2500 gives 111.5 mm
// forward and 111.5 mm sideways: 21.5 mm wider than a turn that fits. Two of
// them one cell apart need 223 mm and have 180.
//
// So the planner refuses and emits stop-and-spin, and it is right about its own
// model.
//
// turn_smooth() never refuses. After a turn it relabels the frame to
// SENSING_POSITION + exit_offset (170 + 30 = 200), and the next turn waits on
// `while (position < FULL_CELL + HALF_CELL - entry_offset)` = 170, which is
// already false. So the wait loop does not run and the second arc begins the
// instant the first ends: the two overlap and she cuts the corner rather than
// returning to the centreline between them.
//
// Which is correct is a question about the floor, not the code. This drives
// the case deliberately: N alternating 90s in consecutive cells with no
// straight between, finishing stopped at a cell centre so the offset can be
// measured against a wall. Mode 1 drives the same path as stop-and-spin for
// comparison -- that is what the planner currently believes she must do.
//
// Needs a 3x3 open section for the default 3 turns: from the start cell she
// ends two cells across and two up.
//
// WHICH ROW. SS90E and SS90 are separate rows precisely so they can be
// different turns, and only the speed-run row has to chain. Searching, she
// stops and senses every cell and wall-following re-centres her, so a wide arc
// costs nothing but a little scrub -- SS90E is tuned and should stay as it is.
// SS90 is the one a fast run strings together, so that is what this drives by
// default, with SS90E available as a comparison rather than as the subject.
//
//   ZIG,turns,mode,first,row   over BT   mode 0 = chained arcs, 1 = spins
//                                        first 1 = right, 0 = left
//                                        row  1 = SS90 (speed run), 0 = SS90E
static int s_zig_turns = 3;
static int s_zig_mode  = 0;
static int s_zig_right = 1;
static int s_zig_row   = 1;    // the speed-run turn: the one that has to chain

static void act_zigzag_test(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }   // release the select press
	HAL_Delay(800);                                             // hands-off settle
	// The row's OWN speed, not SEARCH_SPEED. A speed-run turn tuned at 500 mm/s
	// tells you nothing if the test approaches it at 300, and both modes have to
	// use the same number or the comparison is between two different runs.
	const TurnParameters &row0 = turn_params[s_zig_row ? SS90L : SS90EL];
	const float v = (row0.speed > 0) ? (float)row0.speed : SEARCH_TURN_SPEED;

	report_printf("ZIG,start turns=%d mode=%s first=%c row=%s v=%d\r\n", s_zig_turns,
	              s_zig_mode ? "spin" : "arc", s_zig_right ? 'R' : 'L',
	              s_zig_row ? "SS90" : "SS90E", (int)v);
	control_run_begin();

	// Out of the start cell to its centre, exactly as search_to() begins.
	motion.move(BACK_WALL_TO_CENTER, v, v, SEARCH_ACCELERATION);
	motion.set_position(HALF_CELL);

	int right = s_zig_right;
	for (int i = 0; i < s_zig_turns; i++) {
		const int id = s_zig_row ? (right ? SS90R : SS90L) : (right ? SS90ER : SS90EL);
		const TurnParameters &p = turn_params[id];
		if (s_zig_mode) {
			// Reference: coast to the cell centre, stop, spin, move off again --
			// the same shape turn_back() uses.
			float remaining = (FULL_CELL + HALF_CELL) - motion.position();
			if (remaining > 0.0f) motion.move(remaining, v, 0.0f, SEARCH_ACCELERATION);
			motion.reset_drive_system();
			control_spin(right ? -90.0f : 90.0f, OMEGA_SPIN_TURN, 0.0f, ALPHA_SPIN_TURN);
			motion.move(SENSING_POSITION - HALF_CELL, v, v, SEARCH_ACCELERATION);
			motion.set_position(SENSING_POSITION);
		} else {
			// The case under test. On the first turn the wait loop runs; on every
			// one after it the frame relabel has already put her past the turn
			// point, so the arc fires immediately. That is the chaining.
			motion.set_target_velocity(v);
			float turn_point = FULL_CELL + HALF_CELL - (float)p.entry_offset;
			while (motion.position() < turn_point) { motion.stream_periodic(); }
			motion.turn(p.angle, p.omega, 0.0f, p.alpha);
			motion.set_position(SENSING_POSITION + (float)p.exit_offset);
		}
		report_printf("ZIG,turn %d %c gyro=%d pos=%d\r\n", i + 1, right ? 'R' : 'L',
		              (int)gyro.angle(), (int)motion.position());
		right = !right;
	}

	// Coast to the centre of the cell she finished in, so the stop is a datum
	// you can hold a rule against rather than a place she happened to halt.
	float remaining = (FULL_CELL + HALF_CELL) - motion.position();
	if (remaining > 0.0f) motion.move(remaining, v, 0.0f, SEARCH_ACCELERATION);
	control_run_end();

	// Alternating 90s cancel in pairs, so the net is one turn's worth if the
	// count is odd and nothing if it is even.
	int expect = (s_zig_turns & 1) ? (s_zig_right ? -90 : 90) : 0;
	report_printf("ZIG,done row=%s gyro=%d expect=%d err=%d dist=%d\r\n",
	              s_zig_row ? "SS90" : "SS90E",
	              (int)gyro.angle(), expect, (int)gyro.angle() - expect,
	              (int)odometry.robot_distance());
}

// --- Plan a route over the map she is holding ----------------------------
//
// The planner in native.cpp answers "least time" where the flood answers
// "fewest cells". This runs it three ways over the CURRENT map and streams
// each result to the app, so the three can be laid over the same maze and
// compared: shortest by cells, quickest orthogonally, and quickest with
// diagonals.
//
// Points go out in HALF-CELLS. That is the unit the lattice works in, and it
// is what lets a diagonal be just another line segment as far as the app is
// concerned -- cell centres land on odd coordinates, wall midpoints on mixed,
// and the app needs to know nothing about parity or which wall is which.
//
//   RT,<kind>,<ms>                  kind 0 shortest, 1 quickest, 2 diagonal
//   RP,<u>,<v>,<move>               a vertex; move is plan::Move
//   RTE,<points>,<cells>,<turns>,<spins>
//
// No motors. Works on an injected maze, a remembered one, or one she has just
// explored -- so a whole run can be rehearsed at the bench before she drives.
static plan::Route s_route;      // 1.2 KB: static, never on the stack

// maze.h's Heading and plan::Head are separate enums that happen to agree.
// Nothing enforced that, so assert it here rather than casting and hoping: if
// either is ever reordered this stops the build, instead of the mouse planning
// a route from the wrong start heading and every wall check being 90 degrees
// out.
static_assert((int)NORTH == (int)plan::NN, "Heading/plan::Head disagree on NORTH");
static_assert((int)EAST  == (int)plan::EE, "Heading/plan::Head disagree on EAST");
static_assert((int)SOUTH == (int)plan::SS, "Heading/plan::Head disagree on SOUTH");
static_assert((int)WEST  == (int)plan::WW, "Heading/plan::Head disagree on WEST");

// How much of the line she would fly does she still not know?
//
// Two plans, same maze, opposite assumptions. MASK_CLOSED treats an unseen
// wall as a wall, so it yields the best route she can PROVE. MASK_OPEN treats
// it as an opening, so it yields the best route that could possibly exist --
// a true lower bound, because the real maze has at least as many walls as the
// optimistic view of it.
//
// The gap between them is the most that is still out there to find, and when
// it closes to nothing her route is not "probably" the best, it IS the best
// and no further exploring can change that. The cells worth driving to are
// exactly the not-yet-fully-seen cells ON the optimistic route: an unknown
// cell the optimistic route does not want is an unknown that cannot matter,
// however blank it looks on the screen.
#define UNK_BYTES ((MAZE_WIDTH * MAZE_HEIGHT + 7) / 8)
static uint8_t s_unk_union[UNK_BYTES];    // across all three kinds
static uint8_t s_unk_route[UNK_BYTES];    // this kind alone
static int     s_unk_per   = 0;
static int     s_unk_total = 0;

static inline bool unk_mark(uint8_t *bits, int i) {
	const uint8_t m = (uint8_t)(1u << (i & 7));
	if (bits[i >> 3] & m) return false;
	bits[i >> 3] |= m;
	return true;
}

static void plan_scan_cell(void *, int x, int y) {
	if (x < 0 || x >= MAZE_WIDTH || y < 0 || y >= MAZE_HEIGHT) return;
	if (maze.cell_is_visited(Location((uint8_t)x, (uint8_t)y))) return;
	const int i = y * MAZE_WIDTH + x;
	if (unk_mark(s_unk_route, i)) ++s_unk_per;
	if (unk_mark(s_unk_union, i)) ++s_unk_total;
}

static bool plan_wall_is_exit(const void *, int x, int y, int h) {
	if (x < 0 || x >= MAZE_WIDTH || y < 0 || y >= MAZE_HEIGHT) return false;
	return maze.is_exit(Location((uint8_t)x, (uint8_t)y), (Heading)h);
}
static void plan_emit_point(void *, int u, int v, int mv) {
	report_printf("RP,%d,%d,%d\r\n", u, v, mv);
}

static void act_plan_route(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }

	// Plan against what she KNOWS, not what she hopes: unknown walls closed.
	MazeMask save = maze.get_mask();
	maze.set_mask(MASK_CLOSED);

	// The FAST-RUN speeds, not the search ones. She explores at SEARCH_SPEED
	// because she is reading walls a cell at a time; she runs the known map at
	// RUN_SPEED. Planning the fast run at search speed was why SHORTEST and
	// QUICKEST came back identical to the millisecond -- told that a straight
	// is no quicker than a corner, the planner had nothing to choose between.
	//
	// The ARC speeds below stay as the turn table holds them. They are two
	// different things: she can run a six-cell straight flat out and still take
	// every SS90 at the speed that row was tuned at.
	plan::Robot rb;
	rb.straight.v_max = RUN_SPEED;
	rb.straight.accel = RUN_ACCELERATION;
	rb.straight.decel = RUN_ACCELERATION;
	{
		const TurnParameters &t = turn_params[SS90L];
		rb.arc90_speed = (float)t.speed; rb.arc90_offset = (float)t.entry_offset;
		rb.arc90_omega = t.omega;        rb.arc90_alpha  = t.alpha;
	}
	{
		const TurnParameters &t = turn_params[SS180L];
		rb.arc180_speed = (float)t.speed; rb.arc180_offset = (float)t.entry_offset;
		rb.arc180_omega = t.omega;        rb.arc180_alpha  = t.alpha;
	}
	rb.spin_omega = OMEGA_SPIN_TURN;
	rb.spin_alpha = ALPHA_SPIN_TURN;

	plan::DiagTurns dt;
	{
		const TurnParameters &t = turn_params[SD45L];
		dt.sd45_speed = (float)t.speed; dt.sd45_offset = (float)t.entry_offset;
		dt.sd45_omega = t.omega;        dt.sd45_alpha  = t.alpha;
	}
	{
		const TurnParameters &t = turn_params[DS45L];
		dt.ds45_speed = (float)t.speed; dt.ds45_offset = (float)t.entry_offset;
		dt.ds45_omega = t.omega;        dt.ds45_alpha  = t.alpha;
	}
	dt.diag_v_max = RUN_DIAG_SPEED;   // the narrow corridor, not the straight

	// The clock stops on ENTERING the goal, and the goal is a 2x2 room -- so any
	// of its four cells ends the run. Planning to maze.goal() as a single cell
	// makes her drive to one named square, which when the approach arrives from
	// the far side means crossing the room to reach it: cells and turns spent
	// after she has already finished. On the maze this was first seen on she
	// drove through (8,8) and (8,7), both goal cells, to turn into (7,7).
	//
	// The room only applies when her goal actually sits inside it. The corner
	// presets from 'Set goal' are single cells and must stay single, or she
	// would plan to a 2x2 block hanging off the edge of the arena.
	const Location g = maze.goal();
	const bool goal_is_room = (g.x == GOAL_ROOM_X0 || g.x == GOAL_ROOM_X0 + 1) &&
	                          (g.y == GOAL_ROOM_Y0 || g.y == GOAL_ROOM_Y0 + 1);
	const int gx = goal_is_room ? GOAL_ROOM_X0 : (int)g.x;
	const int gy = goal_is_room ? GOAL_ROOM_Y0 : (int)g.y;
	const int gw = goal_is_room ? 2 : 1;
	const int gh = goal_is_room ? 2 : 1;
	plan::DiagTurns off = dt; off.enabled = false;
	int proven_ms[3] = { -1, -1, -1 };

	for (int kind = 0; kind < 3; kind++) {
		const plan::Objective obj = (kind == 0) ? plan::SHORTEST : plan::QUICKEST;
		const plan::DiagTurns &use = (kind == 2) ? dt : off;
		const plan::WallReader wr = { &plan_wall_is_exit, 0 };
		uint32_t t0 = HAL_GetTick();
		plan_native(s_route, wr, rb, use, obj, START.x, START.y, plan::NN,
		            gx, gy, gw, gh);
		uint32_t took = HAL_GetTick() - t0;

		if (!s_route.ok) {
			report_printf("RT,%d,-1\r\nRTE,0,0,0,0\r\n", kind);
			continue;
		}
		report_printf("RT,%d,%d\r\n", kind, (int)(s_route.seconds * 1000.0f));
		int n = plan::route_points(s_route, START.x, START.y, plan::NN,
		                           plan_emit_point, 0);
		report_printf("RTE,%d,%d,%d,%d\r\n", n, s_route.cells, s_route.turns, s_route.spins);
		report_printf("ACT,plan kind=%d %dms cpu=%lums\r\n",
		              kind, (int)(s_route.seconds * 1000.0f), (unsigned long)took);
		proven_ms[kind] = (int)(s_route.seconds * 1000.0f);
	}

	// --- the same three plans again, with every unseen wall assumed open ---
	maze.set_mask(MASK_OPEN);
	memset(s_unk_union, 0, sizeof(s_unk_union));
	s_unk_total = 0;
	int best_ms = -1;

	for (int kind = 0; kind < 3; kind++) {
		const plan::Objective obj = (kind == 0) ? plan::SHORTEST : plan::QUICKEST;
		const plan::DiagTurns &use = (kind == 2) ? dt : off;
		const plan::WallReader wr = { &plan_wall_is_exit, 0 };
		plan_native(s_route, wr, rb, use, obj, START.x, START.y, plan::NN,
		            gx, gy, gw, gh);
		if (!s_route.ok) { report_printf("RB,%d,-1,0\r\n", kind); continue; }

		memset(s_unk_route, 0, sizeof(s_unk_route));
		s_unk_per = 0;
		plan::route_cells(s_route, START.x, START.y, plan::NN, plan_scan_cell, 0);

		const int ms = (int)(s_route.seconds * 1000.0f);
		if (kind == 2) best_ms = ms;
		report_printf("RB,%d,%d,%d\r\n", kind, ms, s_unk_per);
	}

	// The union: every cell any optimistic route wants and she has not fully
	// seen. This is the exploring still worth doing, and nothing else is.
	for (int y = 0; y < MAZE_HEIGHT; y++)
		for (int x = 0; x < MAZE_WIDTH; x++) {
			const int i = y * MAZE_WIDTH + x;
			if (s_unk_union[i >> 3] & (1u << (i & 7))) report_printf("RU,%d,%d\r\n", x, y);
		}
	report_printf("RUE,%d\r\n", s_unk_total);

	maze.set_mask(save);

	if (s_haveOled) {
		char l[24];
		SSD1306_Fill(SSD1306_COLOR_BLACK);
		SSD1306_GotoXY(0, OLED_TITLE_Y);
		SSD1306_Puts(s_unk_total ? "Plan: more to see" : "Plan: PROVED",
		             &Font_7x10, SSD1306_COLOR_WHITE);
		snprintf(l, sizeof(l), "known %d.%02ds", proven_ms[2] / 1000, (proven_ms[2] % 1000) / 10);
		SSD1306_GotoXY(0, OLED_LIST_Y0);              SSD1306_Puts(l, &Font_7x10, SSD1306_COLOR_WHITE);
		snprintf(l, sizeof(l), "best  %d.%02ds", best_ms / 1000, (best_ms % 1000) / 10);
		SSD1306_GotoXY(0, OLED_LIST_Y0 + OLED_ROW_H); SSD1306_Puts(l, &Font_7x10, SSD1306_COLOR_WHITE);
		snprintf(l, sizeof(l), "unknown %d cells", s_unk_total);
		SSD1306_GotoXY(0, OLED_LIST_Y0 + 2*OLED_ROW_H); SSD1306_Puts(l, &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_UpdateScreen();
	}
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }
	uint8_t junk; while (bt_rx_pop(&junk)) { }
	while (!(SWITCH_LEFT() || SWITCH_RIGHT())) {
		if (bt_rx_pop(&junk)) break;
		HAL_Delay(10);
	}
}

// Launch the ported search brain against the injected/seeded ground-truth
// maze. Two-step: selecting this releases the button, then a fresh press
// launches (search_maze() waits for the start press itself).
static void act_search(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }  // release the select press
	report_write("search armed: press a button to launch\r\n");
	mouse.search_to_goal();   // first light: outbound only (search_maze() adds the return leg)
	report_printf("search done: pose (%d,%d,%d)\r\n",
	              (int)control_pose_x(), (int)control_pose_y(), (int)control_pose_heading());
}

// Dry-run: solve the injected maze with NO motors, animating the pose so you
// can watch her solve any maze on screen without a physical one.
static void act_simulate(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }  // release the select press
	report_write("simulate armed: press a button to launch\r\n");
	mouse.simulate_to_goal();
	report_printf("sim done: pose (%d,%d,%d)\r\n",
	              (int)control_pose_x(), (int)control_pose_y(), (int)control_pose_heading());
}

// Full explore: search to the goal, then back to the start (mapping both ways),
// then report the optimal known route. This is search_maze() with motors.
static void act_explore(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }  // release the select press
	report_write("explore armed: press a button to launch\r\n");
	mouse.search_maze();
	report_printf("explore done: pose (%d,%d,%d)\r\n",
	              (int)control_pose_x(), (int)control_pose_y(), (int)control_pose_heading());
}

// Dry-run of the full explore (to-goal + return + optimal route), NO motors.
static void act_sim_explore(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }  // release the select press
	report_write("sim explore armed: press a button to launch\r\n");
	mouse.simulate_explore();
	report_printf("sim explore done: pose (%d,%d,%d)\r\n",
	              (int)control_pose_x(), (int)control_pose_y(), (int)control_pose_heading());
}

// Recall the last explored maze from EEPROM and report it (no motors). Proves
// the map survived power-off: redraws the remembered maze + optimal route.
static void act_recall(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }  // release the select press
	mouse.recall_from_eeprom();
}

// EEPROM diagnostic: is the 24LC256 present on I2C1, and does a write/read-back
// round-trip? Reports over BT + OLED so we can tell "chip absent" from "bus/driver".
static void act_eeprom_test(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }
	// Scan the whole I2C1 bus first - shows every device that ACKs (OLED @0x3C,
	// and wherever the EEPROM actually sits). Decisive if the address is wrong.
	report_write("EE scan (7-bit addrs that ACK):\r\n");
	int found = 0;
	for (uint8_t a = 0x08; a < 0x78; a++) {
		if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(a << 1), 1, 4) == HAL_OK) {
			report_printf("  0x%02X ack\r\n", a);
			found++;
		}
	}
	report_printf("EE scan: %d device(s)\r\n", found);
	int present = eeprom_present();
	report_printf("EE present=%d (I2C1 @0x50)\r\n", present);
	int match = 0;
	if (present) {
		const uint16_t A = (0x50u << 1);
		// Scratch page, well clear of both tenants: the maze store owns 0..262
		// and the config block sits at 512. This test used to hammer 0x0000,
		// which is the maze store's 'E4M1' magic - so every run of a DIAGNOSTIC
		// silently destroyed the saved maze. Nothing at 1024 belongs to anyone.
		const uint16_t EE_SCRATCH = 1024;
		uint8_t d0[16] = {0};
		HAL_StatusTypeDef sr0 = HAL_I2C_Mem_Read(&hi2c1, A, EE_SCRATCH, I2C_MEMADD_SIZE_16BIT, d0, 16, 300);
		report_printf("EE pre  st=%d: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\r\n",
		              (int)sr0, d0[0],d0[1],d0[2],d0[3],d0[4],d0[5],d0[6],d0[7],d0[8],d0[9],d0[10],d0[11],d0[12],d0[13],d0[14],d0[15]);
		uint8_t w[16]; for (int i = 0; i < 16; i++) w[i] = (uint8_t)(0x10 + i);
		HAL_StatusTypeDef sw = HAL_I2C_Mem_Write(&hi2c1, A, EE_SCRATCH, I2C_MEMADD_SIZE_16BIT, w, 16, 300);
		uint32_t t0 = HAL_GetTick(); int busy = -1;
		for (int i = 0; i < 50; i++) { if (HAL_I2C_IsDeviceReady(&hi2c1, A, 1, 2) == HAL_OK) { busy = (int)(HAL_GetTick()-t0); break; } HAL_Delay(1); }
		uint8_t d1[16] = {0};
		HAL_StatusTypeDef sr1 = HAL_I2C_Mem_Read(&hi2c1, A, EE_SCRATCH, I2C_MEMADD_SIZE_16BIT, d1, 16, 300);
		report_printf("EE post wr=%d busy=%d rd=%d: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\r\n",
		              (int)sw, busy, (int)sr1, d1[0],d1[1],d1[2],d1[3],d1[4],d1[5],d1[6],d1[7],d1[8],d1[9],d1[10],d1[11],d1[12],d1[13],d1[14],d1[15]);
		match = (sr1 == HAL_OK && d1[0] == 0x10 && d1[15] == 0x1F);
	}
	if (s_haveOled) {
		SSD1306_Clear();
		SSD1306_GotoXY(0, 0);
		SSD1306_Puts(present ? "EE: present" : "EE: NO ACK", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, 16);
		SSD1306_Puts(match ? "R/W OK" : (present ? "R/W FAIL" : "@0x50 absent"), &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_UpdateScreen();
		HAL_Delay(2500);
	}
	report_printf("EE test: present=%d rw_ok=%d\r\n", present, match);
}

// Live IR sensor monitor: stream the four ambient-subtracted detector values so
// the wall thresholds can be calibrated on a real maze. Runs until a button.
static void act_ir_monitor(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }
	report_write("IR monitor: move near walls; button or any BT key stops\r\n");
	uint8_t junk;
	while (bt_rx_pop(&junk)) { }   // clear any pending BT so it doesn't insta-stop
	while (!(SWITCH_LEFT() || SWITCH_RIGHT())) {
		if (bt_rx_pop(&junk)) break;   // any BT byte stops the monitor (app-friendly)
		sensors.sample_raw();
		int fsum = sensors.rd_fl + sensors.rd_fr;
		report_printf("IR,%d,%d,%d,%d\r\n", sensors.rd_left, sensors.rd_fl, sensors.rd_fr, sensors.rd_right);
		report_printf("SENS,L%d FL%d FR%d R%d front%d thr(l%d r%d f%d) %s %s\r\n",
		              sensors.rd_left, sensors.rd_fl, sensors.rd_fr, sensors.rd_right, fsum,
		              WALL_THRESH_LEFT, WALL_THRESH_RIGHT, WALL_THRESH_FRONT,
		              sensors.use_real ? "REAL" : "virt",
		              Irs_SM_Enabled() ? "sm" : "blk");
		if (s_haveOled) {
			char l[24];
			SSD1306_Fill(SSD1306_COLOR_BLACK);
			SSD1306_GotoXY(0, OLED_TITLE_Y); SSD1306_Puts("IR monitor", &Font_7x10, SSD1306_COLOR_WHITE);
			snprintf(l, sizeof(l), "L%d  R%d", sensors.rd_left, sensors.rd_right);
			SSD1306_GotoXY(0, 18); SSD1306_Puts(l, &Font_7x10, SSD1306_COLOR_WHITE);
			snprintf(l, sizeof(l), "FL%d FR%d", sensors.rd_fl, sensors.rd_fr);
			SSD1306_GotoXY(0, 30); SSD1306_Puts(l, &Font_7x10, SSD1306_COLOR_WHITE);
			SSD1306_UpdateScreen();
		}
		HAL_Delay(100);   // ~10 Hz
	}
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }
	report_write("IR monitor: stopped\r\n");
}

// Toggle the wall-sensing source: VIRTUAL (ground-truth maze, for sim) <-> REAL
// (the IR detectors). The brain is identical either way; only the source flips.
static void act_sensor_mode(void) {
	sensors.use_real = !sensors.use_real;
	report_printf("Sensor mode: %s\r\n", sensors.use_real ? "REAL (IR)" : "VIRTUAL (truth)");
	if (s_haveOled) {
		SSD1306_Fill(SSD1306_COLOR_BLACK);
		SSD1306_GotoXY(0, OLED_TITLE_Y); SSD1306_Puts("Sensor mode", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, 22); SSD1306_Puts(sensors.use_real ? "REAL (IR)" : "VIRTUAL", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_UpdateScreen();
		HAL_Delay(1200);
	}
}

// Arm/disarm the background wall sampler: the five-state, one-per-tick read
// driven from the 1 kHz control ISR (see IRS.h). Armed, a full ambient-
// subtracted set lands every 5 ms with no busy-wait, so the wall flags stay
// live while the mouse is moving. Disarmed (the default) the sensor layer
// falls back to the blocking batched read.
//
// Numbers shift slightly between the two: the sampler holds each emitter on
// for a whole tick, so the lit sample is taken ~1 ms after switch-on instead
// of ~50 us, and reads a little higher. Re-check the thresholds after arming.
static void act_ir_sampler(void) {
	uint8_t on = !Irs_SM_Enabled();
	Irs_SM_Enable(on);
	report_printf("IR sampler: %s\r\n", on ? "ON (200 Hz, tick-driven)" : "OFF (blocking read)");
	if (s_haveOled) {
		SSD1306_Fill(SSD1306_COLOR_BLACK);
		SSD1306_GotoXY(0, OLED_TITLE_Y); SSD1306_Puts("IR sampler", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, 20); SSD1306_Puts(on ? "ON  200Hz" : "OFF blocking", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, 34); SSD1306_Puts(on ? "tick-driven" : "busy-wait", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_UpdateScreen();
		HAL_Delay(1400);
	}
}

// --- Stubs for actions not built yet (see spec roadmap). Each announces
// itself over BT + OLED so the menu is fully navigable before the feature lands.
static void act_todo(const char *what) {
	report_printf("ACT,todo %s\r\n", what);
	if (s_haveOled) {
		SSD1306_Fill(SSD1306_COLOR_BLACK);
		SSD1306_GotoXY(0, 3);  SSD1306_Puts("Not built yet", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, 26); SSD1306_Puts(what,            &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_UpdateScreen();
		HAL_Delay(1200);
	}
}
static const uint8_t SIZE_PRESETS[] = { 3, 4, 5, 6, MAZE_WIDTH };
static int s_size_idx = (int)(sizeof(SIZE_PRESETS) / sizeof(SIZE_PRESETS[0])) - 1;  // boot = Full
static void act_set_maze_size(void) {           // cycles 3x3 .. 6x6 .. Full on each run
	s_size_idx = (s_size_idx + 1) % (int)(sizeof(SIZE_PRESETS) / sizeof(SIZE_PRESETS[0]));
	uint8_t n = SIZE_PRESETS[s_size_idx];
	maze.set_bounds(n, n);
	report_printf("ACT,size %dx%d\r\n", maze.width(), maze.height());
	if (s_haveOled) {
		char b[20]; snprintf(b, sizeof(b), "Arena %dx%d", maze.width(), maze.height());
		SSD1306_Fill(SSD1306_COLOR_BLACK);
		SSD1306_GotoXY(0, 3);  SSD1306_Puts("Set size", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, 26); SSD1306_Puts(b,          &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_UpdateScreen(); HAL_Delay(900);
	}
	mouse.show_arena();
}
static int s_goal_idx = 3;                       // first run -> Far corner
static void act_set_goal(void) {                // cycles corner/centre presets of the arena
	int w = maze.width(), h = maze.height();
	s_goal_idx = (s_goal_idx + 1) % 4;
	Location g(0, 0); const char *nm = "";
	switch (s_goal_idx) {
		case 0: g = Location((uint8_t)(w - 1), (uint8_t)(h - 1));                 nm = "Far corner"; break;
		case 1: g = Location((uint8_t)(w / 2), (uint8_t)(h / 2));                 nm = "Centre";     break;
		case 2: g = Location((uint8_t)(w > 1 ? 1 : 0), (uint8_t)(h > 1 ? 1 : 0)); nm = "Near start"; break;
		case 3: g = Location((uint8_t)(w - 1), 0);                               nm = "Right edge"; break;
	}
	maze.set_goal(g);
	report_printf("ACT,goal %d,%d\r\n", maze.goal().x, maze.goal().y);
	if (s_haveOled) {
		char b[22]; snprintf(b, sizeof(b), "%s %d,%d", nm, maze.goal().x, maze.goal().y);
		SSD1306_Fill(SSD1306_COLOR_BLACK);
		SSD1306_GotoXY(0, 3);  SSD1306_Puts("Set goal", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, 26); SSD1306_Puts(b,          &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_UpdateScreen(); HAL_Delay(900);
	}
	mouse.show_arena();
}
// Parse up to `max` comma/space-separated floats from `s`; returns the count.
static int tt_parse_floats(const char *s, float *out, int max) {
	int n = 0; char *end;
	while (*s && n < max) {
		while (*s == ',' || *s == ' ') s++;
		if (!*s) break;
		float v = strtof(s, &end);
		if (end == s) break;
		out[n++] = v; s = end;
	}
	return n;
}

// Calibration: GYRO SCALE, measured on the floor and saved to EEPROM.
//
// Why this needs a human in the loop. The spin is gyro-CLOSED-LOOP: the
// controller turns until the gyro says it has reached the target, so
// gyro.angle() comes back at ~the commanded angle every time, by construction.
// The mouse cannot see its own scale error - only that she did not physically
// end up where the gyro thinks she did. So she does the spinning and the
// arithmetic; you supply the one thing she cannot measure, the physical error.
//
// Method: line her up against a straight edge, run 8 x 90 deg = 720 deg (a
// whole number of turns, so she should finish on her starting heading), then
// read off how far SHORT she stopped. Amplifying the error over 720 deg makes a
// fraction of a percent visible to the eye.
//
//   new_scale = old_scale * (commanded - short) / commanded
//
// e.g. 1.02 short by 12 deg over 720 -> 1.02 * 708/720 = 1.003.
//
// BT line commands:
//   G            run the 8 x 90 sequence
//   ERR,<deg>    physical shortfall; positive = stopped short, negative = over
//   GS,<value>   set the scale directly
//   S            save to EEPROM
//   < or q       exit
// Buttons: RIGHT = run the sequence, LEFT = exit.
#define GCAL_SPINS 8
static void act_gyro_scale_cal(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }
	uint8_t junk; while (bt_rx_pop(&junk)) { }

	const float commanded = 90.0f * (float)GCAL_SPINS;

	report_printf("Gyro scale cal: current %d.%03d %s\r\n",
	              (int)(GYRO_SCALE * 1000) / 1000, (int)(GYRO_SCALE * 1000) % 1000,
	              config_store_present() ? "(EEPROM present)" : "(NO EEPROM - cannot save)");
	report_printf("Line her against a straight edge. RIGHT/G = run %dx90=%d deg,\r\n",
	              GCAL_SPINS, (int)commanded);
	report_write("then ERR,<deg short> | GS,<value> sets directly | S saves | < exits\r\n");
	if (s_haveOled) {
		SSD1306_Fill(SSD1306_COLOR_BLACK);
		SSD1306_GotoXY(0, OLED_TITLE_Y);              SSD1306_Puts("Gyro scale",     &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, OLED_LIST_Y0);              SSD1306_Puts("square her up",  &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, OLED_LIST_Y0 + OLED_ROW_H); SSD1306_Puts("R=run  L=exit",  &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_UpdateScreen();
	}

	char line[48]; int len = 0;
	uint8_t lp = 1, rp = 1;
	int run = 1;

	while (run) {
		int do_spin = 0;

		uint8_t l = SWITCH_LEFT(), r = SWITCH_RIGHT();
		if (l && !lp) run = 0;
		if (r && !rp) do_spin = 1;
		lp = l; rp = r;

		uint8_t ch;
		while (bt_rx_pop(&ch)) {
			if (ch == '\r' || ch == '\n') {
				if (len > 0) {
					line[len] = '\0';
					if (strncmp(line, "ERR,", 4) == 0) {
						float e[1] = { 0.0f };
						if (tt_parse_floats(line + 4, e, 1) == 1) {
							float proposed = GYRO_SCALE * (commanded - e[0]) / commanded;
							if (proposed > 0.85f && proposed < 1.15f) {
								int o = (int)(GYRO_SCALE * 1000), n = (int)(proposed * 1000);
								GYRO_SCALE = proposed;
								report_printf("GCAL,old=%d.%03d,short=%d,new=%d.%03d - S to save\r\n",
								              o / 1000, o % 1000, (int)e[0], n / 1000, n % 1000);
							} else {
								// A number this far out is a mis-read or a bad run, not a
								// calibration. Refusing it beats silently wrecking every turn.
								report_printf("GCAL,rejected (would give %d.%03d, expect 0.85-1.15)\r\n",
								              (int)(proposed * 1000) / 1000, (int)(proposed * 1000) % 1000);
							}
						}
					}
					else if (strncmp(line, "GS,", 3) == 0) {
						float v[1] = { 0.0f };
						if (tt_parse_floats(line + 3, v, 1) == 1 && v[0] > 0.85f && v[0] < 1.15f) {
							GYRO_SCALE = v[0];
							report_printf("GCAL,set=%d.%03d - S to save\r\n",
							              (int)(v[0] * 1000) / 1000, (int)(v[0] * 1000) % 1000);
						} else {
							report_write("GCAL,rejected (expect 0.85-1.15)\r\n");
						}
					}
					else if (line[0] == 'S' || line[0] == 's') {
						int ok = config_store_save();
						report_printf("GCAL,saved=%d%s\r\n", ok, ok ? "" : " (no EEPROM or write failed)");
					}
					else if (line[0] == 'G' || line[0] == 'g') { do_spin = 1; }
					else if (line[0] == '<' || line[0] == 'q' || line[0] == 'X') { run = 0; }
					len = 0;
				}
			} else if (len < (int)sizeof(line) - 1) {
				line[len++] = (char)ch;
			} else { len = 0; }
		}

		if (run && do_spin) {
			report_printf("GCAL,run %d spins of 90...\r\n", GCAL_SPINS);
			HAL_Delay(300);
			float measured = 0.0f;
			int aborted = 0;
			for (int i = 0; i < GCAL_SPINS; i++) {
				control_spin(90.0f, OMEGA_SPIN_TURN, 0.0f, ALPHA_SPIN_TURN);
				float a = gyro.angle();
				measured += a;
				// control_spin bails out on a button press or a low battery; a spin
				// that fell well short of 90 means the run is void, not that the
				// gyro is badly scaled.
				if (a < 70.0f || a > 110.0f) { aborted = 1; break; }
				if (s_haveOled) {
					char b[24];
					SSD1306_Fill(SSD1306_COLOR_BLACK);
					SSD1306_GotoXY(0, OLED_TITLE_Y); SSD1306_Puts("Gyro scale", &Font_7x10, SSD1306_COLOR_WHITE);
					snprintf(b, sizeof(b), "spin %d/%d", i + 1, GCAL_SPINS);
					SSD1306_GotoXY(0, OLED_LIST_Y0); SSD1306_Puts(b, &Font_7x10, SSD1306_COLOR_WHITE);
					SSD1306_UpdateScreen();
				}
			}
			if (aborted) {
				report_write("GCAL,aborted - run void, nothing changed\r\n");
			} else {
				// gyro total is a sanity check only: it should land on the commanded
				// figure because the loop closes on the gyro. It is the PHYSICAL
				// angle that carries the information, and only you can read that.
				report_printf("GCAL,done cmd=%d gyro=%d - now send ERR,<deg she is short>\r\n",
				              (int)commanded, (int)measured);
			}
			if (s_haveOled) {
				SSD1306_Fill(SSD1306_COLOR_BLACK);
				SSD1306_GotoXY(0, OLED_TITLE_Y);              SSD1306_Puts("Gyro scale", &Font_7x10, SSD1306_COLOR_WHITE);
				SSD1306_GotoXY(0, OLED_LIST_Y0);              SSD1306_Puts(aborted ? "ABORTED" : "measure error", &Font_7x10, SSD1306_COLOR_WHITE);
				SSD1306_GotoXY(0, OLED_LIST_Y0 + OLED_ROW_H); SSD1306_Puts("send ERR,<deg>", &Font_7x10, SSD1306_COLOR_WHITE);
				SSD1306_UpdateScreen();
			}
			while (bt_rx_pop(&junk)) { }
			lp = SWITCH_LEFT(); rp = SWITCH_RIGHT();
		}
		HAL_Delay(4);
	}
	report_write("Gyro scale cal: exit\r\n");
}

// Diagnostics: LIVE TURN TUNING, driven over Bluetooth (default) or by hand.
// Runs parametric in-place spins and arc turns on command and streams the
// achieved gyro angle + forward distance, so turn dynamics can be tuned to the
// running surface WITHOUT reflashing. Not maze-specific. Line commands over BT:
//   SPIN,<angle>,<omega>,<alpha>
//   ARC,<v>,<angle>,<omega>,<alpha>,<lead_in>,<lead_out>
//   R = repeat last run,  X or < = exit
// Buttons: RIGHT = repeat last run, LEFT = exit. Give the mouse room before a run.
static void act_turn_tune(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }
	uint8_t junk; while (bt_rx_pop(&junk)) { }

	float spin_angle = 90.0f;   // the angle is per-run, not a stored property
	int last_kind = 0;   // 0 none, 1 spin, 2 arc
	int sel = 1;         // which turn ARC edits; SS90ER, the usual one to test

	// ARC now writes STRAIGHT INTO turn_params[sel] instead of a local copy.
	// A tuner that edited a copy could only ever tell you what a turn would
	// have been like - you then transcribed numbers by hand into two other
	// places and hoped. What you tune here is what she searches with.
	report_printf("Turn tune: SPIN,a,w,al | ARC,v,a,w,al,in,out | SEL,0-%d | OUT,mm | S=save | R=repeat | <=exit\r\n",
	              TURN_COUNT - 1);
	report_printf("TUNE,sel=%d %s v=%d in=%d out=%d w=%d al=%d\r\n",
	              sel, turn_names[sel], turn_params[sel].speed, turn_params[sel].entry_offset,
	              turn_params[sel].lead_out, (int)turn_params[sel].omega, (int)turn_params[sel].alpha);
	if (s_haveOled) {
		SSD1306_Fill(SSD1306_COLOR_BLACK);
		SSD1306_GotoXY(0, OLED_TITLE_Y);              SSD1306_Puts("Turn tune",      &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, OLED_LIST_Y0);              SSD1306_Puts("drive from app", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, OLED_LIST_Y0 + OLED_ROW_H); SSD1306_Puts("R=rerun L=exit", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_UpdateScreen();
	}

	char line[64]; int len = 0;
	uint8_t lp = 1, rp = 1;
	int run = 1;

	while (run) {
		int do_run = 0;   // 0 none, 1 spin, 2 arc

		uint8_t l = SWITCH_LEFT(), r = SWITCH_RIGHT();
		if (l && !lp) { run = 0; }
		if (r && !rp) { do_run = last_kind ? last_kind : 1; }
		lp = l; rp = r;

		uint8_t ch;
		while (bt_rx_pop(&ch)) {
			if (ch == '\r' || ch == '\n') {
				if (len > 0) {
					line[len] = '\0';
					if      (strncmp(line, "SPIN,", 5) == 0) {
						// Writes the LIVE spin dynamics, not a scratch copy - so a spin
						// tuned here is the spin she makes at a dead end. Angle stays
						// local: it is what you are asking for this run, not a property
						// of the mouse.
						float a[3] = { spin_angle, OMEGA_SPIN_TURN, ALPHA_SPIN_TURN };
						tt_parse_floats(line + 5, a, 3);
						spin_angle = a[0];
						if (a[1] > 0.0f && a[1] < 2000.0f) OMEGA_SPIN_TURN = a[1];
						if (a[2] > 0.0f && a[2] < 32000.0f) ALPHA_SPIN_TURN = a[2];
						do_run = 1;
					}
					else if (strncmp(line, "ARC,",  4) == 0) {
						// v, angle, omega, alpha, lead_in, lead_out - seeded from the
						// live values so a short command only changes what it names.
						TurnParameters &p = turn_params[sel];
						float a[6] = { (float)p.speed, p.angle, p.omega, p.alpha,
						               (float)p.entry_offset, (float)p.lead_out };
						tt_parse_floats(line + 4, a, 6);
						p.speed        = (int)a[0];
						p.angle        = a[1];
						p.omega        = a[2];
						p.alpha        = a[3];
						p.entry_offset = (int)a[4];
						p.lead_out     = (int)a[5];
						do_run = 2;
					}
					else if (strncmp(line, "OUT,", 4) == 0) {
						// exit_offset is the frame relabel the SEARCH uses. It changes
						// nothing you can watch in a standalone turn, so it is set
						// explicitly rather than riding along with ARC.
						float v[1] = { (float)turn_params[sel].exit_offset };
						if (tt_parse_floats(line + 4, v, 1) == 1) {
							turn_params[sel].exit_offset = (int)v[0];
							report_printf("TUNE,exit=%d (search frame relabel)\r\n", (int)v[0]);
						}
					}
					else if (strncmp(line, "SEL,", 4) == 0) {
						float v[1] = { (float)sel };
						if (tt_parse_floats(line + 4, v, 1) == 1 && v[0] >= 0 && v[0] < TURN_COUNT) {
							sel = (int)v[0];
							const TurnParameters &p = turn_params[sel];
							report_printf("TUNE,sel=%d %s v=%d in=%d out=%d w=%d al=%d\r\n",
							              sel, turn_names[sel], p.speed, p.entry_offset,
							              p.lead_out, (int)p.omega, (int)p.alpha);
						}
					}
					else if (line[0] == 'S') {
						int ok = config_store_save();
						report_printf("TUNE,saved=%d%s\r\n", ok, ok ? "" : " (no EEPROM or write failed)");
					}
					else if (line[0] == 'R' || line[0] == 'r') { do_run = last_kind ? last_kind : 1; }
					else if (line[0] == 'X' || line[0] == '<' || line[0] == 'q') { run = 0; }
					len = 0;
				}
			} else if (len < (int)sizeof(line) - 1) {
				line[len++] = (char)ch;
			} else { len = 0; }
		}

		if (run && do_run) {
			HAL_Delay(300);   // hands-off settle before moving
			if (do_run == 1) {
				control_spin(spin_angle, OMEGA_SPIN_TURN, 0.0f, ALPHA_SPIN_TURN);
				last_kind = 1;
				report_printf("TURNRES,spin,cmd=%d,gyro=%d,dist=%d\r\n",
				              (int)spin_angle, (int)gyro.angle(), (int)odometry.robot_distance());
			} else {
				run_arc_from_table(sel);
				last_kind = 2;
				report_printf("TURNRES,arc,cmd=%d,gyro=%d,dist=%d\r\n",
				              (int)turn_params[sel].angle, (int)gyro.angle(),
				              (int)odometry.robot_distance());
			}
			if (s_haveOled) {
				char b[24];
				SSD1306_Fill(SSD1306_COLOR_BLACK);
				SSD1306_GotoXY(0, OLED_TITLE_Y);              SSD1306_Puts("Turn tune", &Font_7x10, SSD1306_COLOR_WHITE);
				snprintf(b, sizeof(b), "%s cmd%d", last_kind == 1 ? "spin" : "arc",
				         last_kind == 1 ? (int)spin_angle : (int)turn_params[sel].angle);
				SSD1306_GotoXY(0, OLED_LIST_Y0);              SSD1306_Puts(b, &Font_7x10, SSD1306_COLOR_WHITE);
				snprintf(b, sizeof(b), "gyro %d", (int)gyro.angle());
				SSD1306_GotoXY(0, OLED_LIST_Y0 + OLED_ROW_H); SSD1306_Puts(b, &Font_7x10, SSD1306_COLOR_WHITE);
				SSD1306_UpdateScreen();
			}
			while (bt_rx_pop(&junk)) { }   // drain anything queued during the move
			lp = SWITCH_LEFT(); rp = SWITCH_RIGHT();
		}
		HAL_Delay(4);
	}
	report_write("Turn tune: exit\r\n");
}
// ---------------------------------------------------------------------------
// Guided wall-threshold calibration.
//
// Three captures: a DEAD END (both sides + front), OPEN FLOOR (nothing), and a
// CORRIDOR (sides, no front). Not six - one per sensor per state would be more
// precise in principle and a worse procedure in practice, because by the sixth
// repositioning the mouse has been nudged and the earlier samples no longer
// describe the same geometry.
//
// The corridor is the one that is easy to leave out and expensive to leave out.
// The forward pair clips the SIDE walls through its splay, so in a corridor the
// front channel reads far above its open-floor figure with no front wall there
// at all. Set the front threshold from dead-end vs open-floor and it can land
// below that - and she then reports a front wall in every corridor she enters,
// which looks like a maze-solving fault and is not one.
//
// Each capture is a burst reduced by MEDIAN, not mean. One IR sample can be
// spoiled by a stray reflection or a motor transient; a mean quietly absorbs
// that and hands back a number wrong by an amount you cannot see. A median
// throws it away.
//
// The threshold is the least interesting output. What decides whether any of
// this works is the SEPARATION between the two states. A threshold sitting in a
// wide gap tolerates battery sag, a different floor and a warm emitter; one in a
// narrow gap flips on all three, and no tuning fixes a sensor that cannot tell
// the two states apart. So the margin is reported per channel, and a poor one is
// named as poor.
// ---------------------------------------------------------------------------
#define THR_SAMPLES 15

static int thr_median(int *v, int n) {
	// insertion sort; n is 15 and anything cleverer is harder to read for no gain
	for (int i = 1; i < n; i++) {
		int k = v[i], j = i - 1;
		while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; }
		v[j + 1] = k;
	}
	return v[n / 2];
}

static void thr_capture(int *out_l, int *out_r, int *out_f) {
	int L[THR_SAMPLES], R[THR_SAMPLES], F[THR_SAMPLES];
	for (int i = 0; i < THR_SAMPLES; i++) {
		sensors.sample_raw();
		L[i] = sensors.rd_left;
		R[i] = sensors.rd_right;
		F[i] = sensors.rd_fl + sensors.rd_fr;
		HAL_Delay(20);
	}
	*out_l = thr_median(L, THR_SAMPLES);
	*out_r = thr_median(R, THR_SAMPLES);
	*out_f = thr_median(F, THR_SAMPLES);
}

// Judged relative to the present-state reading, not in absolute counts: 40
// counts of separation is comfortable on a channel reading 120 and meaningless
// on one reading 900.
static const char *thr_verdict(int present, int absent) {
	if (present <= absent) return "DEAD";
	int span = present - absent;
	int pct  = (span * 100) / (present > 0 ? present : 1);
	if (pct >= 60) return "good";
	if (pct >= 30) return "ok";
	return "POOR";
}

//   RIGHT / P : capture WALLS PRESENT   (first press)
//   RIGHT / A : capture WALLS ABSENT    (second press)
//   THR,l,r,f : set all three directly
//   S         : save to EEPROM
//   LEFT / <  : exit
static void act_threshold_cal(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }
	uint8_t junk; while (bt_rx_pop(&junk)) { }

	if (!sensors.use_real) {
		report_write("THR,VIRTUAL sensors - switch to REAL first (Sensor mode)\r\n");
	}
	report_printf("Threshold cal: now l=%d r=%d f=%d %s\r\n",
	              WALL_THRESH_LEFT, WALL_THRESH_RIGHT, WALL_THRESH_FRONT,
	              config_store_present() ? "(EEPROM present)" : "(NO EEPROM - cannot save)");
	report_write("1: walls BOTH SIDES + FRONT (a dead end).  RIGHT/P captures\r\n");
	report_write("2: open floor, NO walls in range.          RIGHT/A captures\r\n");
	report_write("3: a CORRIDOR - side walls, NO front wall. RIGHT/C captures\r\n");
	report_write("THR,<l>,<r>,<f> sets directly | S saves | < exits\r\n");

	if (s_haveOled) {
		SSD1306_Fill(SSD1306_COLOR_BLACK);
		SSD1306_GotoXY(0, OLED_TITLE_Y);              SSD1306_Puts("Threshold cal",  &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, OLED_LIST_Y0);              SSD1306_Puts("P dead A open", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, OLED_LIST_Y0 + OLED_ROW_H); SSD1306_Puts("C corr  L=exit", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_UpdateScreen();
	}

	// Three states, because the two obvious ones do not constrain the front.
	// In a corridor the forward pair clips the SIDE walls through its splay, so
	// the front channel reads well above its open-floor figure with no front
	// wall there at all. A threshold set from dead-end vs open-floor can land
	// BELOW that, and she then sees a phantom front wall in every corridor.
	int pl = 0, pr = 0, pf = 0;      // dead end   : sides present, front present
	int al = 0, ar = 0, af = 0;      // open floor : nothing present
	int cl = 0, cr = 0, cf = 0;      // corridor   : sides present, front ABSENT
	int have_present = 0, have_absent = 0, have_corridor = 0;

	char line[48]; int len = 0;
	uint8_t lp = 1, rp = 1;
	int run = 1;
	uint32_t next_show = 0;

	while (run) {
		int do_capture = 0;

		uint8_t l = SWITCH_LEFT(), r = SWITCH_RIGHT();
		if (l && !lp) run = 0;
		if (r && !rp) do_capture = 1;
		lp = l; rp = r;

		uint8_t ch;
		while (bt_rx_pop(&ch)) {
			if (ch == '\r' || ch == '\n') {
				if (len > 0) {
					line[len] = '\0';
					if (strncmp(line, "THR,", 4) == 0) {
						float v[3] = { 0.0f, 0.0f, 0.0f };
						if (tt_parse_floats(line + 4, v, 3) == 3) {
							int nl = (int)v[0], nr = (int)v[1], nf = (int)v[2];
							if (nl > 0 && nl < 4096 && nr > 0 && nr < 4096 && nf > 0 && nf < 8192) {
								WALL_THRESH_LEFT = nl; WALL_THRESH_RIGHT = nr; WALL_THRESH_FRONT = nf;
								report_printf("THR,set l=%d r=%d f=%d - S to save\r\n", nl, nr, nf);
							} else {
								report_write("THR,rejected (expect 1..4095, front 1..8191)\r\n");
							}
						}
					}
					else if (line[0] == 'S' || line[0] == 's') {
						int ok = config_store_save();
						report_printf("THR,saved=%d%s\r\n", ok, ok ? "" : " (no EEPROM or write failed)");
					}
					else if (line[0] == 'P' || line[0] == 'p') { do_capture = 1; }
					else if (line[0] == 'A' || line[0] == 'a') { do_capture = 2; }
					else if (line[0] == 'C' || line[0] == 'c') { do_capture = 3; }
					else if (line[0] == '<' || line[0] == 'q' || line[0] == 'X') { run = 0; }
					len = 0;
				}
			} else if (len < (int)sizeof(line) - 1) {
				line[len++] = (char)ch;
			} else { len = 0; }
		}

		if (run && do_capture) {
			// A button press fills whichever state is still outstanding, in
			// order, so the bench flow is press - reposition - press - reposition
			// - press. P/A/C force a specific one and can be redone at any time.
			int which = do_capture;
			if (which == 1 && have_present) {          // plain RIGHT: next outstanding
				which = !have_absent ? 2 : (!have_corridor ? 3 : 1);
			}

			static const char *SNAME[4] = { "", "DEAD END", "OPEN FLOOR", "CORRIDOR" };
			report_printf("THR,capturing %s...\r\n", SNAME[which]);
			HAL_Delay(250);              // let go of the button before sampling

			if (which == 1) {
				thr_capture(&pl, &pr, &pf);
				have_present = 1;
				report_printf("THR,deadend l=%d r=%d f=%d\r\n", pl, pr, pf);
			} else if (which == 2) {
				thr_capture(&al, &ar, &af);
				have_absent = 1;
				report_printf("THR,openfloor l=%d r=%d f=%d\r\n", al, ar, af);
			} else {
				thr_capture(&cl, &cr, &cf);
				have_corridor = 1;
				report_printf("THR,corridor l=%d r=%d f=%d\r\n", cl, cr, cf);
			}

			if (have_present && have_absent) {
				// The rule for every channel: the threshold goes between the
				// WEAKEST reading where the wall is really there and the STRONGEST
				// where it is not. Anything else is an average of states that do
				// not all matter equally - and it is always the worst case that
				// decides whether she reads the maze correctly.
				//
				// Sides  : present in the dead end AND the corridor; absent on the floor.
				// Front  : present in the dead end only; absent on the floor AND in
				//          the corridor - and the corridor is the one that bites.
				int lp_ = (have_corridor && cl < pl) ? cl : pl;
				int rp_ = (have_corridor && cr < pr) ? cr : pr;
				int fa_ = (have_corridor && cf > af) ? cf : af;

				// Midpoint, unweighted. A missed wall drives her into one she cannot
				// pass; a phantom wall boxes her in. There is no principled reason
				// here to prefer one failure over the other.
				int nl = (lp_ + al) / 2, nr = (rp_ + ar) / 2, nf = (pf + fa_) / 2;
				if (nl > 0 && nr > 0 && nf > 0) {
					WALL_THRESH_LEFT = nl; WALL_THRESH_RIGHT = nr; WALL_THRESH_FRONT = nf;
					report_printf("THR,new l=%d r=%d f=%d - S to save\r\n", nl, nr, nf);
				} else {
					report_write("THR,rejected (a midpoint came out <= 0)\r\n");
				}
				report_printf("THR,margin L %d-%d %s | R %d-%d %s | F %d-%d %s\r\n",
				              al,  lp_, thr_verdict(lp_, al),
				              ar,  rp_, thr_verdict(rp_, ar),
				              fa_, pf,  thr_verdict(pf, fa_));
				if (have_corridor) {
					if (cf > af) {
						report_printf("THR,front bounded by CORRIDOR (%d, floor was %d)\r\n", cf, af);
					}
					if (cf >= pf) {
						// The forward pair cannot tell a front wall from the side
						// walls it is already clipping. No threshold exists.
						report_write("THR,WARN corridor front >= dead-end front - front channel cannot separate\r\n");
					}
				} else {
					report_write("THR,NOTE front not yet checked against a corridor - capture C\r\n");
				}
				// Left and right should be close. If they are not, the fault is in
				// the mounts and no threshold will hide it.
				int bal = pl > pr ? pl - pr : pr - pl;
				int big = pl > pr ? pl : pr;
				if (big > 0 && (bal * 100) / big > 25) {
					report_printf("THR,WARN sides differ %d%% - check mount depth/aim\r\n",
					              (bal * 100) / big);
				}
			}
			while (bt_rx_pop(&junk)) { }
			lp = SWITCH_LEFT(); rp = SWITCH_RIGHT();
			next_show = 0;
		}

		// Live readout at ~5 Hz, so a new threshold can be sanity-checked by
		// moving her about without leaving the routine. The flags are derived
		// here rather than by calling sensors.update(), which would re-sample.
		if (run && HAL_GetTick() >= next_show) {
			next_show = HAL_GetTick() + 200;
			sensors.sample_raw();
			int fs = sensors.rd_fl + sensors.rd_fr;
			report_printf("THR,live l=%d r=%d f=%d -> %c%c%c\r\n",
			              sensors.rd_left, sensors.rd_right, fs,
			              sensors.rd_left  > WALL_THRESH_LEFT  ? 'L' : '-',
			              fs               > WALL_THRESH_FRONT ? 'F' : '-',
			              sensors.rd_right > WALL_THRESH_RIGHT ? 'R' : '-');
			if (s_haveOled) {
				char b[24];
				SSD1306_Fill(SSD1306_COLOR_BLACK);
				SSD1306_GotoXY(0, OLED_TITLE_Y); SSD1306_Puts("Threshold cal", &Font_7x10, SSD1306_COLOR_WHITE);
				snprintf(b, sizeof(b), "L%d R%d F%d", sensors.rd_left, sensors.rd_right, fs);
				SSD1306_GotoXY(0, 18); SSD1306_Puts(b, &Font_7x10, SSD1306_COLOR_WHITE);
				snprintf(b, sizeof(b), "cap %s%s%s", have_present ? "P" : "-",
				         have_absent ? "A" : "-", have_corridor ? "C" : "-");
				SSD1306_GotoXY(0, 30); SSD1306_Puts(b, &Font_7x10, SSD1306_COLOR_WHITE);
				SSD1306_UpdateScreen();
			}
		}

		HAL_Delay(4);
	}
	report_write("Threshold cal: exit\r\n");
}

// --- Wall follower: one hand on the wall, no map needed to decide ----------
//
// Its own competition class, not a weaker solver. A wall-follower course is
// built with a wall connected to the centre, so a follower always arrives; a
// maze-solver maze is built with the inside cut off from the outside, so one
// never can. Pointing this at a solver maze and watching it not arrive is a
// demonstration of the maze, not of the code.
//
// Four entries rather than one action with a hand setting, because the hand IS
// the experiment: left and right walk different halves of a course and the two
// rarely take the same line. Having to change a setting between two runs you
// want to compare back to back is friction with no payoff.
//
// She maps as she goes either way, so a lap leaves those walls in the map.
static void act_wall_follow_l(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }
	report_write("wall follow (left hand) armed: press a button to launch\r\n");
	mouse.follow_to(maze.goal(), false);
}
static void act_wall_follow_r(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }
	report_write("wall follow (right hand) armed: press a button to launch\r\n");
	mouse.follow_to(maze.goal(), true);
}
static void act_sim_follow_l(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }
	report_write("sim wall follow (left hand) armed: press a button to launch\r\n");
	mouse.simulate_follow(false);
}
static void act_sim_follow_r(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }
	report_write("sim wall follow (right hand) armed: press a button to launch\r\n");
	mouse.simulate_follow(true);
}
static void act_speed_run(void)     { act_todo("Speed run"); }
static void act_resume_saved(void)  { act_todo("Resume saved"); }
static void act_run_options(void)   { act_todo("Run options"); }

// --- Emitter hold: light one / all IR emitters for camera-based aiming. ------
// Driven at ~50% high-frequency PWM so the average current stays well under the
// SFH 4550's 100 mA DC limit whatever the emitter rail; a camera integrates the
// tens-of-kHz switching into a steady spot. RIGHT / 'E' cycles the target,
// LEFT / '<' exits (all emitters off).
static inline void emit_pwm_wait(void) { for (volatile uint32_t c = 180; c; c--) { } }
static void act_ir_emitter_hold(void) {
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }
	const ADCSensors ids[4] = { IR_SIDE_LEFT, IR_FRONT_LEFT, IR_FRONT_RIGHT, IR_SIDE_RIGHT };
	const char *names[5] = { "SL side-left", "FL front-left", "FR front-right", "SR side-right", "ALL four" };
	int sel = 0, run = 1, redraw = 1;
	uint8_t rp = 1, lp = 1; uint32_t note = 0;
	report_write("Emitter hold (camera aid): ~50% PWM, safe DC. RIGHT/E=next, LEFT/<=exit.\r\n");
	// This action pokes the emitter GPIOs directly, so the background sampler
	// has to stand down or the two fight over the same four pins. Restored on
	// exit so arming it is not silently undone by a visit here.
	uint8_t sm_was = Irs_SM_Enabled();
	if (sm_was) Irs_SM_Enable(0);
	while (run) {
		for (int i = 0; i < 300; i++) {                       // ~50% PWM burst
			if (sel == 4) { for (int k = 0; k < 4; k++) Irs_Emitter_Set(ids[k], 1); }
			else          { Irs_Emitter_Set(ids[sel], 1); }
			emit_pwm_wait();
			if (sel == 4) { for (int k = 0; k < 4; k++) Irs_Emitter_Set(ids[k], 0); }
			else          { Irs_Emitter_Set(ids[sel], 0); }
			emit_pwm_wait();
		}
		uint8_t r = SWITCH_RIGHT(), l = SWITCH_LEFT();
		if (r && !rp) { sel = (sel + 1) % 5; redraw = 1; }
		if (l && !lp) { run = 0; }
		rp = r; lp = l;
		uint8_t ch;
		while (bt_rx_pop(&ch)) {
			if (ch == 'E' || ch == 'n' || ch == 'r' || ch == ' ') { sel = (sel + 1) % 5; redraw = 1; }
			else if (ch == '<' || ch == 'q') { run = 0; }
		}
		if (run && (redraw || HAL_GetTick() - note >= 600)) {
			note = HAL_GetTick(); redraw = 0;
			report_printf("EMIT,%d,%s\r\n", sel, names[sel]);
			if (s_haveOled) {
				SSD1306_Fill(SSD1306_COLOR_BLACK);
				SSD1306_GotoXY(0, OLED_TITLE_Y);              SSD1306_Puts("Emitter hold", &Font_7x10, SSD1306_COLOR_WHITE);
				SSD1306_GotoXY(0, OLED_LIST_Y0);              SSD1306_Puts(names[sel],      &Font_7x10, SSD1306_COLOR_WHITE);
				SSD1306_GotoXY(0, OLED_LIST_Y0 + OLED_ROW_H); SSD1306_Puts("R=next L=exit", &Font_7x10, SSD1306_COLOR_WHITE);
				SSD1306_UpdateScreen();
			}
		}
	}
	for (int k = 0; k < 4; k++) Irs_Emitter_Set(ids[k], 0);
	if (sm_was) Irs_SM_Enable(1);          // restore the sampler if it was armed
	report_write("Emitter hold: off\r\n");
}

// Diagnostics: show the firmware version + build stamp until a button / BT key.
static void act_fw_version(void) {
	report_printf("VER,%s,%s\r\n", FW_VERSION, FW_BUILD);
	if (s_haveOled) {
		char l[24];
		SSD1306_Fill(SSD1306_COLOR_BLACK);
		SSD1306_GotoXY(0, OLED_TITLE_Y);                SSD1306_Puts("Firmware", &Font_7x10, SSD1306_COLOR_WHITE);
		snprintf(l, sizeof(l), "v%s", FW_VERSION);
		SSD1306_GotoXY(0, OLED_LIST_Y0);                SSD1306_Puts(l, &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, OLED_LIST_Y0 + OLED_ROW_H);   SSD1306_Puts(__DATE__, &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY(0, OLED_LIST_Y0 + 2*OLED_ROW_H); SSD1306_Puts(__TIME__, &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_UpdateScreen();
	}
	while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); }   // wait for release
	uint8_t junk; while (bt_rx_pop(&junk)) { }                  // clear pending BT
	while (!(SWITCH_LEFT() || SWITCH_RIGHT())) {                // hold until a press / BT key
		if (bt_rx_pop(&junk)) break;
		HAL_Delay(10);
	}
}

// Flat action registry. `key` is the BT shortcut (the app sends it to run the
// action directly, bypassing the OLED menu hierarchy). Order matters: the
// categories below index into this array.
typedef struct { const char *name; char key; menu_action_t run; } MenuItem;
static const MenuItem MENU[] = {
	/* 0*/ { "Forward 180", 'f', act_forward     },
	/* 1*/ { "Right 90",    'd', act_right90     },
	/* 2*/ { "Left 90",     'a', act_left90      },
	/* 3*/ { "Spin 180",    's', act_spin180     },
	/* 4*/ { "Motion test", 'o', act_motion_test },
	/* 5*/ { "Search",      'h', act_search      },
	/* 6*/ { "Simulate",    'u', act_simulate    },
	/* 7*/ { "Explore",     'e', act_explore     },
	/* 8*/ { "Sim explore", 'x', act_sim_explore },
	/* 9*/ { "Recall maze", 'm', act_recall      },
	/*10*/ { "IR monitor",  'i', act_ir_monitor  },
	/*11*/ { "Sensor mode", 'v', act_sensor_mode },
	/*12*/ { "Recal gyro",  'g', act_recal_gyro  },
	/*13*/ { "Reset pose",  'z', act_reset_pose  },
	/*14*/ { "Test mode",   'k', act_toggle_test },
	/*15*/ { "EEPROM test", 't', act_eeprom_test },
	/*16*/ { "BT ->57600",  'b', act_set_bt_57k  },
	/*17*/ { "Set maze size",'c', act_set_maze_size },
	/*18*/ { "Set goal",     'y', act_set_goal      },
	/*19*/ { "Turn tuning",  'j', act_turn_tune     },
	/*20*/ { "Wall follow L", 'w', act_wall_follow_l },
	/*21*/ { "Speed run",    'l', act_speed_run     },
	/*22*/ { "Resume saved", 'R', act_resume_saved  },
	/*23*/ { "Run options",  'O', act_run_options   },
	/*24*/ { "Emitter hold", 'E', act_ir_emitter_hold },
	/*25*/ { "Firmware ver", 'V', act_fw_version },
	/*26*/ { "IR sampler",   'S', act_ir_sampler },
	/*27*/ { "Gyro scale cal",'G', act_gyro_scale_cal },
	/*28*/ { "BT provision", 'B', act_bt_provision },
	/*29*/ { "Threshold cal",'T', act_threshold_cal },
	/*30*/ { "Zigzag test",  'Z', act_zigzag_test   },
	/*31*/ { "Plan route",   'P', act_plan_route    },
	/*32*/ { "Wall follow R",'W', act_wall_follow_r },
	/*33*/ { "Sim follow L", 'q', act_sim_follow_l  },
	/*34*/ { "Sim follow R", 'Q', act_sim_follow_r  },
};
static const int MENU_N = (int)(sizeof(MENU) / sizeof(MENU[0]));

// Categories group items; modes group categories. Nav is 3-level:
// MODE -> CATEGORY -> ITEM. Wheels scroll, RIGHT enters/runs, LEFT backs out.
// BT keys above bypass all of this.  (*) marks a stub, not built yet.
typedef struct { const char *name; const uint8_t *items; uint8_t n; } Category;
static const uint8_t CAT_CAL[]    = { 12, 27, 29, 10, 19, 4, 30 };  // Recal gyro, Gyro scale cal, Threshold cal, IR monitor, Turn tuning, Motion test, Zigzag test
static const uint8_t CAT_MOVES[]  = { 0, 1, 2, 3 };         // Forward, Right90, Left90, Spin180
static const uint8_t CAT_INMAZE[] = { 17, 18, 5 };          // Set size*, Set goal*, Search
static const uint8_t CAT_SIM[]    = { 6, 8, 9 };            // Simulate, Sim explore, Recall maze
static const uint8_t CAT_DIAG[]   = { 15, 11, 26, 13, 14, 16, 28, 24, 25 }; // EEPROM test, Sensor mode, IR sampler, Reset pose, Test mode, BT57600, BT provision, Emitter hold, Firmware ver
static const uint8_t CAT_WALL[]   = { 20, 32, 33, 34 };     // Wall follow L/R, and both simulated
static const uint8_t CAT_SOLVE[]  = { 7, 21, 22, 31 };     // Explore, Speed run*, Resume saved*, Plan route
static const uint8_t CAT_RUNOPT[] = { 23 };                 // Run options*
static const Category CAT[] = {
	/*0*/ { "Calibration", CAT_CAL,    7 },
	/*1*/ { "Moves",       CAT_MOVES,  4 },
	/*2*/ { "In-maze",     CAT_INMAZE, 3 },
	/*3*/ { "Simulation",  CAT_SIM,    3 },
	/*4*/ { "Diagnostics", CAT_DIAG,   9 },
	/*5*/ { "Wall follow", CAT_WALL,   4 },
	/*6*/ { "Maze solver", CAT_SOLVE,  4 },
	/*7*/ { "Run options", CAT_RUNOPT, 1 },
};

// Two top-level modes, each grouping a set of categories (by CAT[] index).
typedef struct { const char *name; const uint8_t *cats; uint8_t n; } Mode;
static const uint8_t MODE_TEST[] = { 0, 1, 2, 3, 4 };   // Calibration, Moves, In-maze, Simulation, Diagnostics
static const uint8_t MODE_COMP[] = { 5, 6, 7 };         // Wall follow, Maze solver, Run options
static const Mode MODE[] = {
	{ "TEST / BENCH", MODE_TEST, 5 },
	{ "COMPETITION",  MODE_COMP, 3 },
};
static const int NUM_MODE = (int)(sizeof(MODE) / sizeof(MODE[0]));

// Position of category c within mode m (for LEFT/back navigation).
static int cat_pos_in_mode(int m, int c) {
	for (int i = 0; i < (int)MODE[m].n; i++) if (MODE[m].cats[i] == c) return i;
	return 0;
}

// Where an item sits in the MODE -> CATEGORY -> ITEM tree.
//
// A BT key runs an action directly, without walking the menu, so without this
// the OLED carries on showing whichever page it was left on while she does
// something else entirely. That matters because the display is the ONLY thing
// you can read when the app is not in your hand - at a competition it is what
// tells you she is doing what you meant.
static int menu_locate(int item, int *m_out, int *c_out, int *s_out) {
	for (int m = 0; m < NUM_MODE; m++) {
		for (int i = 0; i < (int)MODE[m].n; i++) {
			int c = MODE[m].cats[i];
			for (int k = 0; k < (int)CAT[c].n; k++) {
				if (CAT[c].items[k] == item) { *m_out = m; *c_out = c; *s_out = k; return 1; }
			}
		}
	}
	return 0;   // an item no category lists: leave the menu where it was
}

static const int MENU_VIS  = 3;    // visible list rows in the blue band (y=18,30,42)
static const int MENU_JOG_COUNTS = 800;  // wheel counts (both wheels summed) per cursor step
static const int MENU_JOG_DIR    = -1;    // flip to -1 if scrolling feels inverted


static void menu_render(int level, int mode, int cat, int sel, int top)
{
	if (!s_haveOled) return;   // headless: nothing to draw, drive via BT
	char ln[24];
	SSD1306_Fill(SSD1306_COLOR_BLACK);
	SSD1306_GotoXY(0, OLED_TITLE_Y);
	if (level == 0)
		snprintf(ln, sizeof(ln), "E4 MODE%s", control_test_mode() ? " T" : "");
	else if (level == 1)
		snprintf(ln, sizeof(ln), "%s%s", MODE[mode].name, control_test_mode() ? " T" : "");
	else
		snprintf(ln, sizeof(ln), "%s%s", CAT[cat].name, control_test_mode() ? " T" : "");
	SSD1306_Puts(ln, &Font_7x10, SSD1306_COLOR_WHITE);
	int count = (level == 0) ? NUM_MODE : (level == 1) ? (int)MODE[mode].n : (int)CAT[cat].n;
	for (int r = 0; r < MENU_VIS; r++) {
		int i = top + r;
		if (i >= count) break;
		int y = OLED_LIST_Y0 + r * OLED_ROW_H;   // 18,30,42 -- all in blue
		char cur = (i == sel) ? '>' : ' ';
		if (level == 0) {
			snprintf(ln, sizeof(ln), "%c%s", cur, MODE[i].name);
		} else if (level == 1) {
			snprintf(ln, sizeof(ln), "%c%s", cur, CAT[MODE[mode].cats[i]].name);
		} else {
			int mi = CAT[cat].items[i];
			if (MENU[mi].run == act_toggle_test)
				snprintf(ln, sizeof(ln), "%c%s %s", cur, MENU[mi].name, control_test_mode() ? "ON" : "OFF");
			else
				snprintf(ln, sizeof(ln), "%c%s", cur, MENU[mi].name);
		}
		SSD1306_GotoXY(0, y);
		SSD1306_Puts(ln, &Font_7x10, SSD1306_COLOR_WHITE);
	}
	SSD1306_UpdateScreen();
}

void app_main()
{
	// initialise modules
	LED_ALL_ON();				// All LEDs lit
	MPU_Initialise();			// Communication for MPU and initial setting of Gyro
	s_haveOled = SSD1306_Init();   // 0 if no OLED on the bus (bare test board)
	Motor_Initialize();
	Motor_StopPWM();
	Encoder_Initialize();

	report_write("E4 boot\r\n");
	report_printf("VER,%s,%s\r\n", FW_VERSION, FW_BUILD);

	// Settings block: gyro scale (and later the wall thresholds) come from the
	// EEPROM if one is fitted and holds a valid block. No chip is not an error -
	// the compiled defaults stand and nothing persists.
	config_store_begin();

	{
		char vln[24];
		SSD1306_GotoXY (0, 0);
		snprintf(vln, sizeof(vln), "E4  v%s", FW_VERSION);
		SSD1306_Puts (vln, &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY (0, 16);
		SSD1306_Puts ("(c) James Clarke", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY (0, 36);
		SSD1306_Puts (__DATE__, &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_GotoXY (0, 50);
		SSD1306_Puts (__TIME__, &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_UpdateScreen(); //display
	}

	HAL_Delay(1600);

	SSD1306_Clear();
	SSD1306_GotoXY (0,0);


	// --- Phase 2/4: bring up the 1 kHz motion control loop (+ gyro) ---
	// control_begin() calibrates the gyro (hold still ~0.6 s), then arms the
	// SysTick control ISR in IDLE: loop + odometry + gyro live, motors OFF.
	SSD1306_Clear();
	SSD1306_GotoXY (0, 0);
	SSD1306_Puts ("Gyro calibrating", &Font_7x10, SSD1306_COLOR_WHITE);
	SSD1306_GotoXY (0, 26);
	SSD1306_Puts ("hold still...", &Font_7x10, SSD1306_COLOR_WHITE);
	SSD1306_UpdateScreen();
	control_begin();
	bt_rx_init();          // interrupt-driven BT receive (menu + maze injection)
	SSD1306_Clear();

	// Sensor-free bring-up: seed a known ground-truth maze for the virtual
	// sensor and set a near goal that matches it (one right turn from start).
	truth_load_default();
	maze.set_goal(Location(1, 1));

	int level = 0, mode = 0, cat = 0, sel = 0, top = 0;
	uint8_t left_prev = 0, right_prev = 0;
	uint32_t stream_then = 0;
	long jog_ref = (long)MENU_JOG_DIR * ((long)odometry.total_left() + odometry.total_right());

	menu_render(level, mode, cat, sel, top);
	report_write("E4 ready. Wheels scroll; RIGHT=enter/run, LEFT=back. BT: <key> runs an action, n/p scroll, r enter, < back\r\n");

	while (1)
	{
		// keep the controller's battery estimate fresh (blocking ADC: main loop only)
		control_update_battery(Battery_GetVoltage());

		// idle heartbeat only: a slow 1 Hz pose so the app shows she is there.
		if (HAL_GetTick() - stream_then >= 1000) {
			stream_then = HAL_GetTick();
			control_stream_telemetry();
		}

		bool dirty   = false;
		int  run_idx = -1;                                   // MENU index to run, or -1
		int  count   = (level == 0) ? NUM_MODE : (level == 1) ? (int)MODE[mode].n : (int)CAT[cat].n;

		// --- buttons: RIGHT = enter/run, LEFT = back ---
		uint8_t l = SWITCH_LEFT();
		uint8_t r = SWITCH_RIGHT();
		if (r && !right_prev) {
			if      (level == 0) { mode = sel; level = 1; sel = 0; top = 0; dirty = true; }         // enter mode
			else if (level == 1) { cat = MODE[mode].cats[sel]; level = 2; sel = 0; top = 0; dirty = true; }  // enter category
			else                 { run_idx = CAT[cat].items[sel]; }                              // run item
		}
		if (l && !left_prev) {
			if      (level == 2) { level = 1; sel = cat_pos_in_mode(mode, cat); top = 0; dirty = true; }     // back to categories
			else if (level == 1) { level = 0; sel = mode; top = 0; dirty = true; }                           // back to modes
		}
		left_prev  = l;
		right_prev = r;
		count = (level == 0) ? NUM_MODE : (level == 1) ? (int)MODE[mode].n : (int)CAT[cat].n;    // level may have changed

		// --- wheel jog: spin/roll the wheels to scroll the current view ---
		long jog = (long)MENU_JOG_DIR * ((long)odometry.total_left() + odometry.total_right());
		while (jog - jog_ref >=  MENU_JOG_COUNTS) { if (sel < count - 1) sel++; jog_ref += MENU_JOG_COUNTS; dirty = true; }
		while (jog - jog_ref <= -MENU_JOG_COUNTS) { if (sel > 0) sel--; jog_ref -= MENU_JOG_COUNTS; dirty = true; }

		// --- BT input: line-buffered (menu commands + maze injection) ---
		static char bt_line[80];
		static int  bt_len = 0;
		uint8_t ch;
		while (bt_rx_pop(&ch)) {   // drain the interrupt-filled receive buffer
			if (ch == '\n' || ch == '\r') {
				if (bt_len > 0) {
					bt_line[bt_len] = '\0';
					if (strncmp(bt_line, "SIZE,", 5) == 0) {
						int nw = atoi(bt_line + 5);
						const char *cc = strchr(bt_line + 5, ',');
						int nh = cc ? atoi(cc + 1) : nw;
						if (nw > 0 && nh > 0) { maze.set_bounds((uint8_t)nw, (uint8_t)nh); mouse.show_arena(); }
					}
					else if (strncmp(bt_line, "VER?", 4) == 0) {
						// A QUERY, not the menu action. act_fw_version holds the OLED
						// and waits for a button, which is right for a person reading
						// it off the display and wrong for an app asking "who are you?"
						// on connect - that looked like a hang on a screen almost
						// identical to the boot splash, and left the menu parked in
						// Diagnostics. A query answers and changes nothing.
						report_printf("VER,%s,%s\r\n", FW_VERSION, FW_BUILD);
					}
					else if (strncmp(bt_line, "CFG?", 4) == 0) {
						// Everything she is actually holding, including the twelve turn
						// rows nothing drives yet - those are invisible otherwise, and a
						// slot you cannot inspect is a slot you cannot trust.
						config_store_report(1);
					}
					else if (strncmp(bt_line, "THR?", 4) == 0) {
						report_printf("THR,now l=%d r=%d f=%d\r\n",
						              WALL_THRESH_LEFT, WALL_THRESH_RIGHT, WALL_THRESH_FRONT);
					}
					else if (strncmp(bt_line, "THR,", 4) == 0) {
						// Also accepted inside "Threshold cal"; here so the app can set
						// and save thresholds without driving the menu into a routine.
						float v[3] = { 0.0f, 0.0f, 0.0f };
						if (tt_parse_floats(bt_line + 4, v, 3) == 3) {
							int nl = (int)v[0], nr = (int)v[1], nf = (int)v[2];
							if (nl > 0 && nl < 4096 && nr > 0 && nr < 4096 && nf > 0 && nf < 8192) {
								WALL_THRESH_LEFT = nl; WALL_THRESH_RIGHT = nr; WALL_THRESH_FRONT = nf;
								int ok = config_store_save();
								report_printf("THR,set l=%d r=%d f=%d saved=%d\r\n", nl, nr, nf, ok);
							} else {
								report_write("THR,rejected (expect 1..4095, front 1..8191)\r\n");
							}
						}
					}
					else if (strncmp(bt_line, "SPD?", 4) == 0) {
						report_printf("SPD,now v=%d a=%d diag=%d\r\n",
						              (int)RUN_SPEED, (int)RUN_ACCELERATION, (int)RUN_DIAG_SPEED);
					}
					else if (strncmp(bt_line, "SPD,", 4) == 0) {
						// Fast-run speeds, swept from the app the way the turn table is.
						// Seeded from the live values so a short command changes only
						// what it names. Nothing drives on these yet -- they are what the
						// planner believes she can do, so a bad one costs an estimate.
						float v[3] = { RUN_SPEED, RUN_ACCELERATION, RUN_DIAG_SPEED };
						tt_parse_floats(bt_line + 4, v, 3);
						if (v[0] >= 100.0f && v[0] <= 3000.0f &&
						    v[1] >= 100.0f && v[1] <= 32000.0f &&
						    v[2] >= 100.0f && v[2] <= 3000.0f) {
							RUN_SPEED = v[0]; RUN_ACCELERATION = v[1]; RUN_DIAG_SPEED = v[2];
							int ok = config_store_save();
							report_printf("SPD,set v=%d a=%d diag=%d saved=%d\r\n",
							              (int)RUN_SPEED, (int)RUN_ACCELERATION,
							              (int)RUN_DIAG_SPEED, ok);
						} else {
							report_write("SPD,rejected (v 100..3000, a 100..32000, diag 100..3000)\r\n");
						}
					}
					else if (strncmp(bt_line, "ZIG,", 4) == 0) {
						// Seeded from the live values, so a short command changes only
						// what it names. Sets up the next run; it does not launch one.
						float z[4] = { (float)s_zig_turns, (float)s_zig_mode,
						               (float)s_zig_right, (float)s_zig_row };
						tt_parse_floats(bt_line + 4, z, 4);
						if (z[0] >= 1.0f && z[0] <= 8.0f) s_zig_turns = (int)z[0];
						s_zig_mode  = (z[1] != 0.0f);
						s_zig_right = (z[2] != 0.0f);
						s_zig_row   = (z[3] != 0.0f);
						report_printf("ZIG,set turns=%d mode=%s first=%c row=%s\r\n", s_zig_turns,
						              s_zig_mode ? "spin" : "arc", s_zig_right ? 'R' : 'L',
						              s_zig_row ? "SS90" : "SS90E");
					}
					else if (strncmp(bt_line, "GOAL,", 5) == 0) {
						const char *a = bt_line + 5; const char *cc = strchr(a, ',');
						if (cc) { maze.set_goal(Location((uint8_t)atoi(a), (uint8_t)atoi(cc + 1))); mouse.show_arena(); }
					}
					else if (!maze_inject_line(bt_line) && bt_len == 1) {
						char c = bt_line[0];
						if      (c == 'n' || c == '+') { if (sel < count - 1) { sel++; dirty = true; } }
						else if (c == 'p' || c == '-') { if (sel > 0) { sel--; dirty = true; } }
						else if (c == 'r' || c == ' ') {                        // enter / run (as RIGHT)
							if      (level == 0) { mode = sel; level = 1; sel = 0; top = 0; dirty = true; }
							else if (level == 1) { cat = MODE[mode].cats[sel]; level = 2; sel = 0; top = 0; dirty = true; }
							else                 { run_idx = CAT[cat].items[sel]; }
						}
						else if (c == '<' || c == 'q') {                        // back (as LEFT)
							if      (level == 2) { level = 1; sel = cat_pos_in_mode(mode, cat); top = 0; dirty = true; }
							else if (level == 1) { level = 0; sel = mode; top = 0; dirty = true; }
						}
						else {                                                  // direct action key
							for (int i = 0; i < MENU_N; i++) if (MENU[i].key == c) { run_idx = i; break; }
						}
					}
					bt_len = 0;
				}
			} else if (bt_len < (int)sizeof(bt_line) - 1) {
				bt_line[bt_len++] = (char)ch;
			} else {
				bt_len = 0;   // overflow: drop the line
			}
		}

		// --- run the chosen action (blocking; moves abort on a button press) ---
		if (run_idx >= 0 && run_idx < MENU_N) {
			// Point the menu at whatever is about to run, so the OLED and the app
			// agree regardless of which one started it. On exit the normal redraw
			// then leaves the cursor sitting on the item you just ran.
			{
				int m2, c2, s2;
				if (menu_locate(run_idx, &m2, &c2, &s2)) {
					mode = m2; cat = c2; sel = s2; level = 2; top = 0;
				}
			}
			if (s_haveOled) {
				// Actions with their own display overwrite this immediately; the
				// ones without it leave the name up, which is what you want.
				SSD1306_Fill(SSD1306_COLOR_BLACK);
				SSD1306_GotoXY(0, OLED_TITLE_Y); SSD1306_Puts("Running",            &Font_7x10, SSD1306_COLOR_WHITE);
				SSD1306_GotoXY(0, OLED_LIST_Y0); SSD1306_Puts(MENU[run_idx].name,   &Font_7x10, SSD1306_COLOR_WHITE);
				SSD1306_UpdateScreen();
			}
			LED_RIGHT_ON();
			report_printf("RUN,%d,%s\r\n", run_idx, MENU[run_idx].name);
			MENU[run_idx].run();
			report_printf("DONE,%d gyro_a=%d d=%d\r\n",
			              run_idx, (int)gyro.angle(), (int)odometry.robot_distance());
			LED_RIGHT_OFF();
			left_prev  = SWITCH_LEFT();   // swallow a still-held button
			right_prev = SWITCH_RIGHT();
			jog_ref = (long)MENU_JOG_DIR * ((long)odometry.total_left() + odometry.total_right());  // moves reset totals
			dirty = true;
		}

		// --- redraw only on change (no flicker) ---
		if (dirty) {
			count = (level == 0) ? NUM_MODE : (level == 1) ? (int)MODE[mode].n : (int)CAT[cat].n;
			if (sel >= count) sel = count - 1;
			if (sel < 0)      sel = 0;
			if (sel < top)                top = sel;
			if (sel > top + MENU_VIS - 1) top = sel - (MENU_VIS - 1);
			menu_render(level, mode, cat, sel, top);
		}

		HAL_Delay(20);
	}
}

// temp functions for testing


