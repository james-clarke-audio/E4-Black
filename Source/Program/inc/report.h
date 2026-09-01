/*
 * report.h
 *
 * Minimal debug reporting over USART1 (the HC-05 / Bluetooth serial port,
 * 57600 baud 8N1). Self-contained: depends only on the maze object. The full
 * mazerunner reporting.h (sensor/motor/motion telemetry) will be ported once
 * those subsystems exist.
 */
#ifndef REPORT_H
#define REPORT_H

#include "maze.h"

// Maze print styles
enum MazeView { PLAIN, COSTS, DIRS };

void report_write(const char *s);           // send a string over USART1
void report_printf(const char *fmt, ...);   // printf-style over USART1
void report_maze(int style = COSTS);        // dump the maze (walls + costs/dirs)

// --- Maze-app telemetry protocol (see e4-maze.html) ---
void report_pose(float x_mm, float y_mm, float heading_deg);            // "POS,x,y,deg"
void report_tel(unsigned long t_ms, float v, float omega, float dist,
                float ang, float gyro, float batt);                    // "TEL,..."

#endif // REPORT_H
