#include <config.h>
#include "app_main.h"
#include "version.h"   // FW_VERSION / FW_BUILD
#include "ssd1306.h"
#include "mpu9250.h"
#include "IRS.h"
#include "PWM.h"
#include "encoders.h"
#include "maze.h"        // Phase 1: ported flood-fill maze solver
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
#include "config_store.h" // versioned settings block (gyro scale, later thresholds)
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
static void act_right90 (void) { control_arc_turn(300.0f, 102.0f, -90.0f, 170.0f, 1000.0f, 90.0f); }
static void act_left90  (void) { control_arc_turn(300.0f, 102.0f,  90.0f, 170.0f, 1000.0f, 90.0f); }
static void act_spin180 (void) { control_spin(180.0f, 180.0f, 0.0f, 1000.0f); }

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
		uint8_t d0[16] = {0};
		HAL_StatusTypeDef sr0 = HAL_I2C_Mem_Read(&hi2c1, A, 0x0000, I2C_MEMADD_SIZE_16BIT, d0, 16, 300);
		report_printf("EE pre  st=%d: %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\r\n",
		              (int)sr0, d0[0],d0[1],d0[2],d0[3],d0[4],d0[5],d0[6],d0[7],d0[8],d0[9],d0[10],d0[11],d0[12],d0[13],d0[14],d0[15]);
		uint8_t w[16]; for (int i = 0; i < 16; i++) w[i] = (uint8_t)(0x10 + i);
		HAL_StatusTypeDef sw = HAL_I2C_Mem_Write(&hi2c1, A, 0x0000, I2C_MEMADD_SIZE_16BIT, w, 16, 300);
		uint32_t t0 = HAL_GetTick(); int busy = -1;
		for (int i = 0; i < 50; i++) { if (HAL_I2C_IsDeviceReady(&hi2c1, A, 1, 2) == HAL_OK) { busy = (int)(HAL_GetTick()-t0); break; } HAL_Delay(1); }
		uint8_t d1[16] = {0};
		HAL_StatusTypeDef sr1 = HAL_I2C_Mem_Read(&hi2c1, A, 0x0000, I2C_MEMADD_SIZE_16BIT, d1, 16, 300);
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
		report_printf("SENS,L%d FL%d FR%d R%d front%d thr(s%d f%d) %s %s\r\n",
		              sensors.rd_left, sensors.rd_fl, sensors.rd_fr, sensors.rd_right, fsum,
		              sensors.thresh_side, sensors.thresh_front, sensors.use_real ? "REAL" : "virt",
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

	// Last-run parameters, seeded from the search-turn defaults.
	float spin[3] = { 90.0f, OMEGA_SPIN_TURN, ALPHA_SPIN_TURN };                    // angle, omega, alpha
	float arc[6]  = { SEARCH_TURN_SPEED, -90.0f, 170.0f, 2500.0f, 100.0f, 30.0f };  // v, angle, omega, alpha, in, out
	int last_kind = 0;   // 0 none, 1 spin, 2 arc

	report_write("Turn tune ready: SPIN,a,w,al | ARC,v,a,w,al,in,out | R=repeat <=exit\r\n");
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
					if      (strncmp(line, "SPIN,", 5) == 0) { tt_parse_floats(line + 5, spin, 3); do_run = 1; }
					else if (strncmp(line, "ARC,",  4) == 0) { tt_parse_floats(line + 4, arc,  6); do_run = 2; }
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
				control_spin(spin[0], spin[1], 0.0f, spin[2]);   // (angle, top_omega, final_omega=0, alpha)
				last_kind = 1;
				report_printf("TURNRES,spin,cmd=%d,gyro=%d,dist=%d\r\n",
				              (int)spin[0], (int)gyro.angle(), (int)odometry.robot_distance());
			} else {
				control_arc_turn(arc[0], arc[4], arc[1], arc[2], arc[3], arc[5]); // (v, lead_in, angle, omega, alpha, lead_out)
				last_kind = 2;
				report_printf("TURNRES,arc,cmd=%d,gyro=%d,dist=%d\r\n",
				              (int)arc[1], (int)gyro.angle(), (int)odometry.robot_distance());
			}
			if (s_haveOled) {
				char b[24];
				SSD1306_Fill(SSD1306_COLOR_BLACK);
				SSD1306_GotoXY(0, OLED_TITLE_Y);              SSD1306_Puts("Turn tune", &Font_7x10, SSD1306_COLOR_WHITE);
				snprintf(b, sizeof(b), "%s cmd%d", last_kind == 1 ? "spin" : "arc",
				         last_kind == 1 ? (int)spin[0] : (int)arc[1]);
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
static void act_wall_follow(void)   { act_todo("Wall follower"); }
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
	/*20*/ { "Wall follower",'w', act_wall_follow   },
	/*21*/ { "Speed run",    'l', act_speed_run     },
	/*22*/ { "Resume saved", 'R', act_resume_saved  },
	/*23*/ { "Run options",  'O', act_run_options   },
	/*24*/ { "Emitter hold", 'E', act_ir_emitter_hold },
	/*25*/ { "Firmware ver", 'V', act_fw_version },
	/*26*/ { "IR sampler",   'S', act_ir_sampler },
	/*27*/ { "Gyro scale cal",'G', act_gyro_scale_cal },
	/*28*/ { "BT provision", 'B', act_bt_provision },
};
static const int MENU_N = (int)(sizeof(MENU) / sizeof(MENU[0]));

// Categories group items; modes group categories. Nav is 3-level:
// MODE -> CATEGORY -> ITEM. Wheels scroll, RIGHT enters/runs, LEFT backs out.
// BT keys above bypass all of this.  (*) marks a stub, not built yet.
typedef struct { const char *name; const uint8_t *items; uint8_t n; } Category;
static const uint8_t CAT_CAL[]    = { 12, 27, 10, 19, 4 };  // Recal gyro, Gyro scale cal, IR monitor, Turn tuning, Motion test
static const uint8_t CAT_MOVES[]  = { 0, 1, 2, 3 };         // Forward, Right90, Left90, Spin180
static const uint8_t CAT_INMAZE[] = { 17, 18, 5 };          // Set size*, Set goal*, Search
static const uint8_t CAT_SIM[]    = { 6, 8, 9 };            // Simulate, Sim explore, Recall maze
static const uint8_t CAT_DIAG[]   = { 15, 11, 26, 13, 14, 16, 28, 24, 25 }; // EEPROM test, Sensor mode, IR sampler, Reset pose, Test mode, BT57600, BT provision, Emitter hold, Firmware ver
static const uint8_t CAT_WALL[]   = { 20 };                 // Wall follower*
static const uint8_t CAT_SOLVE[]  = { 7, 21, 22 };          // Explore, Speed run*, Resume saved*
static const uint8_t CAT_RUNOPT[] = { 23 };                 // Run options*
static const Category CAT[] = {
	/*0*/ { "Calibration", CAT_CAL,    5 },
	/*1*/ { "Moves",       CAT_MOVES,  4 },
	/*2*/ { "In-maze",     CAT_INMAZE, 3 },
	/*3*/ { "Simulation",  CAT_SIM,    3 },
	/*4*/ { "Diagnostics", CAT_DIAG,   9 },
	/*5*/ { "Wall follow", CAT_WALL,   1 },
	/*6*/ { "Maze solver", CAT_SOLVE,  3 },
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


