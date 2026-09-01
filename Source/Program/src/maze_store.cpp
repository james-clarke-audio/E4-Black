#include "maze_store.h"
#include "maze.h"
#include "eeprom.h"

extern Maze maze;

static const uint16_t MS_BASE  = 0;
static const int      MS_WALLS = MAZE_CELL_COUNT;   // 256
static const int      MS_LEN   = 6 + MS_WALLS + 1;  // header(6) + walls + checksum

// 8-bit additive checksum over goal(2) + walls(256): bytes [4 .. 4+2+256-1]
static uint8_t ms_checksum(const uint8_t *p) {
  uint8_t s = 0;
  for (int i = 0; i < 2 + MS_WALLS; i++) s = (uint8_t)(s + p[i]);
  return s;
}

bool maze_store_save() {
  uint8_t buf[6 + MAZE_CELL_COUNT + 1];
  buf[0] = 'E'; buf[1] = '4'; buf[2] = 'M'; buf[3] = '1';
  buf[4] = maze.goal().x;
  buf[5] = maze.goal().y;
  maze.save_to(&buf[6]);
  buf[6 + MS_WALLS] = ms_checksum(&buf[4]);
  return eeprom_write(MS_BASE, buf, MS_LEN) != 0;
}

bool maze_store_valid() {
  uint8_t buf[6 + MAZE_CELL_COUNT + 1];
  if (!eeprom_read(MS_BASE, buf, MS_LEN)) return false;
  if (!(buf[0] == 'E' && buf[1] == '4' && buf[2] == 'M' && buf[3] == '1')) return false;
  return ms_checksum(&buf[4]) == buf[6 + MS_WALLS];
}

bool maze_store_load() {
  uint8_t buf[6 + MAZE_CELL_COUNT + 1];
  if (!eeprom_read(MS_BASE, buf, MS_LEN)) return false;
  if (!(buf[0] == 'E' && buf[1] == '4' && buf[2] == 'M' && buf[3] == '1')) return false;
  if (ms_checksum(&buf[4]) != buf[6 + MS_WALLS]) return false;
  maze.set_goal(Location(buf[4], buf[5]));
  maze.load_from(&buf[6]);
  return true;
}
