#include <config.h>
#include "app_main.h"
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
#include <string.h>

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

static void act_stub(void) {
	report_write("not implemented yet\r\n");
	for (int i = 0; i < 6; i++) { LED_ALL_TOGGLE(); HAL_Delay(70); }
	LED_ALL_OFF();
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
		report_printf("SENS,L%d FL%d FR%d R%d front%d thr(s%d f%d) %s\r\n",
		              sensors.rd_left, sensors.rd_fl, sensors.rd_fr, sensors.rd_right, fsum,
		              sensors.thresh_side, sensors.thresh_front, sensors.use_real ? "REAL" : "virt");
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
};
static const int MENU_N = (int)(sizeof(MENU) / sizeof(MENU[0]));

// Grouped categories for the OLED (2-level menu). Wheels scroll, RIGHT enters/
// runs, LEFT goes back. BT keys above bypass all of this.
typedef struct { const char *name; const uint8_t *items; uint8_t n; } Category;
static const uint8_t CAT_MOVE[]    = { 0, 1, 2, 3, 4 };
static const uint8_t CAT_MAZE[]    = { 5, 6, 7, 8, 9 };
static const uint8_t CAT_SENSORS[] = { 10, 11 };
static const uint8_t CAT_SETUP[]   = { 12, 13, 14, 15, 16 };
static const Category CAT[] = {
	{ "Move",    CAT_MOVE,    5 },
	{ "Maze",    CAT_MAZE,    5 },
	{ "Sensors", CAT_SENSORS, 2 },
	{ "Setup",   CAT_SETUP,   5 },
};
static const int NUM_CAT = (int)(sizeof(CAT) / sizeof(CAT[0]));

static const int MENU_VIS  = 3;    // visible list rows in the blue band (y=18,30,42)
static const int MENU_JOG_COUNTS = 800;  // wheel counts (both wheels summed) per cursor step
static const int MENU_JOG_DIR    = -1;    // flip to -1 if scrolling feels inverted

// Two-colour 128x64 SSD1306 on E4: rows 0..15 are YELLOW, rows 16..63 are
// BLUE, with a single non-displayed pixel line at the seam (~y16). Never let
// a text line straddle the seam or it falls in the dead row. Title -> yellow,
// list -> blue. Tune here if a different panel is fitted.
static const int OLED_TITLE_Y = 3;    // title baseline, inside the yellow band
static const int OLED_LIST_Y0 = 18;   // first list row, just below the seam (blue)
static const int OLED_ROW_H   = 12;   // line pitch in the blue band (3 rows: 18,30,42)

static void menu_render(int level, int cat, int sel, int top)
{
	if (!s_haveOled) return;   // headless: nothing to draw, drive via BT
	char ln[24];
	SSD1306_Fill(SSD1306_COLOR_BLACK);
	SSD1306_GotoXY(0, OLED_TITLE_Y);
	if (level == 0)
		snprintf(ln, sizeof(ln), "E4 MENU %s", control_test_mode() ? "TEST" : "");
	else
		snprintf(ln, sizeof(ln), "%s%s", CAT[cat].name, control_test_mode() ? " T" : "");
	SSD1306_Puts(ln, &Font_7x10, SSD1306_COLOR_WHITE);
	int count = (level == 0) ? NUM_CAT : (int)CAT[cat].n;
	for (int r = 0; r < MENU_VIS; r++) {
		int i = top + r;
		if (i >= count) break;
		int y = OLED_LIST_Y0 + r * OLED_ROW_H;   // 18,30,42 -- all in blue
		char cur = (i == sel) ? '>' : ' ';
		if (level == 0) {
			snprintf(ln, sizeof(ln), "%c%s", cur, CAT[i].name);
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

	SSD1306_GotoXY (0, 0);
	SSD1306_Puts ("Welcome to E4", &Font_7x10, SSD1306_COLOR_WHITE);
	SSD1306_GotoXY (0, 20);
	SSD1306_Puts ("(c) James Clarke", &Font_7x10, SSD1306_COLOR_WHITE);
	SSD1306_UpdateScreen(); //display

	HAL_Delay(1000);

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

	int level = 0, cat = 0, sel = 0, top = 0;
	uint8_t left_prev = 0, right_prev = 0;
	uint32_t stream_then = 0;
	long jog_ref = (long)MENU_JOG_DIR * ((long)odometry.total_left() + odometry.total_right());

	menu_render(level, cat, sel, top);
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
		int  count   = (level == 0) ? NUM_CAT : (int)CAT[cat].n;

		// --- buttons: RIGHT = enter/run, LEFT = back ---
		uint8_t l = SWITCH_LEFT();
		uint8_t r = SWITCH_RIGHT();
		if (r && !right_prev) {
			if (level == 0) { level = 1; cat = sel; sel = 0; top = 0; dirty = true; }   // enter category
			else            { run_idx = CAT[cat].items[sel]; }                          // run item
		}
		if (l && !left_prev) {
			if (level == 1) { level = 0; sel = cat; top = 0; dirty = true; }            // back to categories
		}
		left_prev  = l;
		right_prev = r;
		count = (level == 0) ? NUM_CAT : (int)CAT[cat].n;    // level may have changed

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
					if (!maze_inject_line(bt_line) && bt_len == 1) {
						char c = bt_line[0];
						if      (c == 'n' || c == '+') { if (sel < count - 1) { sel++; dirty = true; } }
						else if (c == 'p' || c == '-') { if (sel > 0) { sel--; dirty = true; } }
						else if (c == 'r' || c == ' ') {                        // enter / run (as RIGHT)
							if (level == 0) { level = 1; cat = sel; sel = 0; top = 0; dirty = true; }
							else            { run_idx = CAT[cat].items[sel]; }
						}
						else if (c == '<' || c == 'q') {                        // back (as LEFT)
							if (level == 1) { level = 0; sel = cat; top = 0; dirty = true; }
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
			count = (level == 0) ? NUM_CAT : (int)CAT[cat].n;
			if (sel >= count) sel = count - 1;
			if (sel < 0)      sel = 0;
			if (sel < top)                top = sel;
			if (sel > top + MENU_VIS - 1) top = sel - (MENU_VIS - 1);
			menu_render(level, cat, sel, top);
		}

		HAL_Delay(20);
	}
}

// temp functions for testing


