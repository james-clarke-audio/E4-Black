/*
 * report.cpp
 *
 * UART output primitive on USART1 plus a maze dump. The wall/cost formatting
 * is adapted from the original mazerunner reporting.h print_maze().
 */
#include "report.h"
#include "usart.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;   // defined by CubeMX (also declared in usart.h)

static char s_buf[160];

void report_write(const char *s) {
  HAL_UART_Transmit(&huart1, (uint8_t *)s, (uint16_t)strlen(s), 200);
}

void report_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(s_buf, sizeof(s_buf), fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (n > (int)sizeof(s_buf) - 1) n = sizeof(s_buf) - 1;
  HAL_UART_Transmit(&huart1, (uint8_t *)s_buf, (uint16_t)n, 200);
}

// ---- maze printing (adapted from mazerunner reporting.h) ----
static const char POST = 'o';

static void print_h_wall(uint8_t state) {
  if (state == EXIT)         report_write("   ");
  else if (state == WALL)    report_write("---");
  else if (state == VIRTUAL) report_write("###");
  else                       report_write("...");   // UNKNOWN
}

static void print_north_walls(int y) {
  for (int x = 0; x < MAZE_WIDTH; x++) {
    report_printf("%c", POST);
    WallInfo walls = maze.walls(Location(x, y));
    print_h_wall(walls.north & maze.get_mask());
  }
  report_printf("%c\r\n", POST);
}

static void print_south_walls(int y) {
  for (int x = 0; x < MAZE_WIDTH; x++) {
    report_printf("%c", POST);
    WallInfo walls = maze.walls(Location(x, y));
    print_h_wall(walls.south & maze.get_mask());
  }
  report_printf("%c\r\n", POST);
}

void report_maze(int style) {
  const char dirChars[] = "^>v<* ";
  maze.flood(maze.goal());
  for (int y = MAZE_HEIGHT - 1; y >= 0; y--) {
    print_north_walls(y);
    for (int x = 0; x < MAZE_WIDTH; x++) {
      Location loc(x, y);
      WallInfo walls = maze.walls(loc);
      uint8_t state = walls.west & maze.get_mask();
      if (state == EXIT)         report_write(" ");
      else if (state == WALL)    report_write("|");
      else if (state == VIRTUAL) report_write("#");
      else                       report_write(":");
      if (style == COSTS) {
        report_printf("%3d", (int)maze.cost(loc));
      } else if (style == DIRS) {
        unsigned char direction = maze.heading_to_smallest(loc, NORTH);
        if (loc == maze.goal()) direction = DIRECTION_COUNT;
        char arrow = ' ';
        if (direction != BLOCKED) arrow = dirChars[direction];
        report_printf(" %c ", arrow);
      } else {
        report_write("   ");
      }
    }
    report_write("|\r\n");
  }
  print_south_walls(0);
  report_write("\r\n");
}

// --- Maze-app telemetry protocol emitters ---
void report_pose(float x, float y, float deg) {
  report_printf("POS,%d,%d,%d\r\n", (int)x, (int)y, (int)deg);
}

void report_tel(unsigned long t, float v, float omega, float dist,
                float ang, float gyro, float batt) {
  int bv = (int)batt;
  int bf = (int)(batt * 100) % 100; if (bf < 0) bf = -bf;
  report_printf("TEL,%lu,%d,%d,%d,%d,%d,%d.%02d\r\n",
                t, (int)v, (int)omega, (int)dist, (int)ang, (int)gyro, bv, bf);
}
