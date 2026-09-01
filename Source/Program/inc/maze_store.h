/*
 * maze_store.h  --  persist the discovered maze (global `maze`) to the 24LC256
 * EEPROM and restore it, so a solved maze survives power-off.
 *
 * EEPROM layout @ address 0:
 *   [0..3]    magic 'E','4','M','1'
 *   [4]       goal.x     [5] goal.y
 *   [6..261]  256 wall bytes (Maze::save_to, index x*16+y)
 *   [262]     checksum = 8-bit sum of bytes [4..261]
 */
#ifndef MAZE_STORE_H
#define MAZE_STORE_H

bool maze_store_save();   // serialize `maze` (+goal) to EEPROM; true on success
bool maze_store_load();   // verify + load into `maze`; true if a valid save loaded
bool maze_store_valid();  // true if a valid saved maze is present (header+checksum)

#endif  // MAZE_STORE_H
