/*
 * mouse.cpp  --  the single definition of the global Mouse. Behaviour is
 * inline in mouse.h (mazerunner-style header).
 */
#include "mouse.h"

Mouse mouse;

// The one definition of the turn table. Lives here rather than in the header
// so there is exactly one copy in the image; mouse_config.h declares it extern.
TurnParameters turn_params[4] = {
    // speed, entry, exit, lead_out, angle, omega, alpha, trigger
    { (int)SEARCH_TURN_SPEED, 100, 30, 90,  90.0f, 170.0f, 2500.0f, TURN_THRESHOLD_SS90E }, // 0 SS90EL
    { (int)SEARCH_TURN_SPEED, 100, 30, 90, -90.0f, 170.0f, 2500.0f, TURN_THRESHOLD_SS90E }, // 1 SS90ER
    { (int)SEARCH_TURN_SPEED, 100, 30, 90,  90.0f, 170.0f, 2500.0f, TURN_THRESHOLD_SS90E }, // 2 SS90L
    { (int)SEARCH_TURN_SPEED, 100, 30, 90, -90.0f, 170.0f, 2500.0f, TURN_THRESHOLD_SS90E }, // 3 SS90R
};

const char *const turn_names[4] = { "SS90EL", "SS90ER", "SS90L", "SS90R" };
