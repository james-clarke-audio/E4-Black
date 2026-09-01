/*
 * robot_switches.h  --  the two user buttons, mazerunner "switches" API.
 * Stands in for UKMARSBOT's function switch; E4 has LEFT/RIGHT buttons.
 */
#ifndef ROBOT_SWITCHES_H
#define ROBOT_SWITCHES_H
#include "config.h"   // SWITCH_LEFT/RIGHT, HAL_Delay

class Switches {
 public:
  bool button_pressed() { return SWITCH_LEFT() || SWITCH_RIGHT(); }
  void wait_for_button_release() { while (SWITCH_LEFT() || SWITCH_RIGHT()) { HAL_Delay(5); } }
};
extern Switches switches;
#endif  // ROBOT_SWITCHES_H
