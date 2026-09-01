/*
 * reporter.h  --  minimal action logging for the maze solver, over BT.
 * The sensor-calibration/test reporters from mazerunner are omitted (those
 * test functions are not ported until real sensors exist).
 */
#ifndef REPORTER_H
#define REPORTER_H
#include "maze.h"     // Location, Heading
#include "report.h"   // report_printf

class Reporter {
 public:
  // action: F/L/R/B/#/-/*  note: 's'(sensor) 'd'(distance) ' '  loc + heading
  void log_action_status(char action, char note, Location loc, Heading hdg) {
    report_printf("ACT,%c,%c,%d,%d,%d\r\n", action, (note == ' ') ? '-' : note,
                  loc.x, loc.y, (int)hdg);
  }
};
extern Reporter reporter;
#endif  // REPORTER_H
