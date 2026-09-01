/*
 * maze.cpp
 *
 * The single global Maze instance. The original Arduino build defined this
 * in the .ino; in this project it lives in its own translation unit.
 *
 * TODO (Phase 6): persist across power-off via the on-board I2C EEPROM.
 */
#include "maze.h"

Maze maze;
