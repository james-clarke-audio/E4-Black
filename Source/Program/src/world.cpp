/*
 * world.cpp  --  the "world" the brain reads during sensor-free bring-up:
 * the global stub objects (sensors/switches/reporter) and a ground-truth maze
 * ('truth') the virtual sensor consults. Replace truth via the app-injection
 * parser or the builders below.
 */
#include "robot_sensors.h"
#include "robot_switches.h"
#include "reporter.h"
#include "maze.h"
#include "report.h"   // report_write (injection acks)

// Global instances the ported brain talks to.
Maze           truth;
VirtualSensors sensors;
Switches       switches;
Reporter       reporter;

// Fully-open KNOWN maze: every interior wall EXIT, every border WALL.
static void truth_build_open() {
  for (int x = 0; x < MAZE_WIDTH; x++) {
    for (int y = 0; y < MAZE_HEIGHT; y++) {
      Location c(x, y);
      truth.set_wall(c, NORTH, EXIT);
      truth.set_wall(c, EAST, EXIT);
      truth.set_wall(c, SOUTH, EXIT);
      truth.set_wall(c, WEST, EXIT);
    }
  }
  for (int x = 0; x < MAZE_WIDTH; x++) {
    truth.set_wall(Location(x, 0), SOUTH, WALL);
    truth.set_wall(Location(x, MAZE_HEIGHT - 1), NORTH, WALL);
  }
  for (int y = 0; y < MAZE_HEIGHT; y++) {
    truth.set_wall(Location(0, y), WEST, WALL);
    truth.set_wall(Location(MAZE_WIDTH - 1, y), EAST, WALL);
  }
  truth.set_mask(MASK_CLOSED);  // fully known
}

void truth_clear() { truth_build_open(); }

// Set one cell's walls from a bitmask (N=1,E=2,S=4,W=8; bit set = WALL). Both
// sides are kept consistent by Maze::set_wall.
void truth_set_cell(int x, int y, int mask) {
  Location c(x, y);
  truth.set_wall(c, NORTH, (mask & 1) ? WALL : EXIT);
  truth.set_wall(c, EAST, (mask & 2) ? WALL : EXIT);
  truth.set_wall(c, SOUTH, (mask & 4) ? WALL : EXIT);
  truth.set_wall(c, WEST, (mask & 8) ? WALL : EXIT);
}

// Default bring-up maze -- forces exactly one right turn from the start:
//   (0,0): N open, E walled  -> move ahead to (0,1)
//   (0,1): N walled, E open  -> turn right into (1,1) = goal
void truth_load_default() {
  truth_build_open();
  truth.set_wall(Location(0, 0), EAST, WALL);
  truth.set_wall(Location(0, 1), NORTH, WALL);
}


// --- Ground-truth maze injection over BT --------------------------------
// The app streams a loaded maze as text lines; each handled line is acked with
// "GTOK" so the app can pace. Wall mask bits: N=1, E=2, S=4, W=8.
//   GTC              clear to a fully-open maze (start of a new upload)
//   GTR,y,<16 hex>[,cc]  one row: 16 cells (single hex digit = wall mask),
//                    optional 2-hex checksum cc. If present and it does not
//                    match, the row is REJECTED (acked "GTERR") so the app can
//                    re-send it; a good/checksum-less row is applied and acked
//                    "GTOK". Checksum = (y + sum over x of (x+1)*nibble) & 0xFF.
//   GTG,x,y          set the goal cell
//   GTE              end of upload: the mouse streams its stored truth back as
//                    "GTV,y,<16 hex>" rows (for the app to verify), then "GTDONE"
static int gt_hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}
// Row checksum shared with the app: y folded in, each cell weighted by (x+1)
// so a single garbled nibble or a swap changes it.
static int gt_rowsum(int y, const int *cells) {
  int c = y & 0xFF;
  for (int x = 0; x < MAZE_WIDTH; x++) c = (c + (x + 1) * cells[x]) & 0xFF;
  return c;
}

static const char *gt_int(const char *p, int *out) {
  if (*p == ',') p++;
  int v = 0;
  while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
  *out = v;
  return p;
}

bool maze_inject_line(const char *s) {
  if (!(s[0] == 'G' && s[1] == 'T')) return false;
  char cmd = s[2];
  if (cmd == 'C') {                       // clear
    truth_clear();
    report_write("GTOK\r\n");
  } else if (cmd == 'R') {                // row of 16 hex masks [+ checksum]
    int y = 0;
    const char *p = gt_int(s + 3, &y);
    if (*p == ',') p++;
    if (y < 0 || y >= MAZE_HEIGHT) { report_write("GTERR\r\n"); return true; }
    int cells[MAZE_WIDTH];
    int n = 0;
    for (int x = 0; x < MAZE_WIDTH; x++) {
      if (p[x] <= ' ' || p[x] == ',') break;
      cells[x] = gt_hex(p[x]);
      n++;
    }
    if (n != MAZE_WIDTH) { report_write("GTERR\r\n"); return true; }  // short/garbled row
    const char *q = p + MAZE_WIDTH;                                    // optional ",cc"
    if (*q == ',') {
      int got = (gt_hex(q[1]) << 4) | gt_hex(q[2]);
      if (got != gt_rowsum(y, cells)) { report_write("GTERR\r\n"); return true; }
    }
    for (int x = 0; x < MAZE_WIDTH; x++) truth_set_cell(x, y, cells[x]);
    report_write("GTOK\r\n");
  } else if (cmd == 'G') {                // goal (echoes the goal it set back)
    int x = 0, y = 0;
    const char *p = gt_int(s + 3, &x);
    p = gt_int(p, &y);
    if (x >= 0 && x < MAZE_WIDTH && y >= 0 && y < MAZE_HEIGHT) {
      maze.set_goal(Location(x, y));
      report_printf("GTGOK,%d,%d\r\n", x, y);   // echo so the app can verify + retry
    } else {
      report_write("GTERR\r\n");
    }
  } else if (cmd == 'E') {                // end of upload: read the truth back
    for (int y = 0; y < MAZE_HEIGHT; y++) {
      char row[28];
      int i = 0;
      row[i++] = 'G'; row[i++] = 'T'; row[i++] = 'V'; row[i++] = ',';
      if (y >= 10) row[i++] = '0' + (y / 10);
      row[i++] = '0' + (y % 10);
      row[i++] = ',';
      for (int x = 0; x < MAZE_WIDTH; x++) {
        WallInfo w = truth.walls(Location(x, y));
        int m = (w.north == WALL ? 1 : 0) | (w.east == WALL ? 2 : 0) |
                (w.south == WALL ? 4 : 0) | (w.west == WALL ? 8 : 0);
        row[i++] = "0123456789abcdef"[m & 0xF];
      }
      row[i++] = '\r'; row[i++] = '\n'; row[i] = 0;
      report_write(row);
    }
    report_printf("GTQ,%d,%d\r\n", maze.goal().x, maze.goal().y);  // goal read-back
    report_write("GTDONE\r\n");
  } else {
    report_write("GTERR\r\n");
  }
  return true;
}
