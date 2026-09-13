/*
 * mouse.cpp  --  the single definition of the global Mouse. Behaviour is
 * inline in mouse.h (mazerunner-style header).
 */
#include "mouse.h"

Mouse mouse;

// The one definition of the turn table. Lives here rather than in the header
// so there is exactly one copy in the image; mouse_config.h declares it extern.
// In-place spin dynamics. One definition, same as the arcs.
float OMEGA_SPIN_TURN = 360.0f;    // deg/s
float ALPHA_SPIN_TURN = 3600.0f;   // deg/s/s

TurnParameters turn_params[TURN_COUNT] = {
    // speed, entry, exit, lead_out, angle, omega, alpha, trigger
    //
    // omega is not a free choice: R = v / omega, so for a wanted radius at
    // 300 mm/s, omega(deg/s) = (300 / R) * 180/pi. The radius each of these
    // was derived from is in the comment. Only the SS90E pair has ever met a
    // floor; every other row is arithmetic waiting to be measured.
    { (int)SEARCH_TURN_SPEED, 100, 30, 90,    90.0f, 170.0f, 2500.0f, TURN_THRESHOLD_SS90E },  //  0 SS90EL  search 90, the only turns she makes today
    { (int)SEARCH_TURN_SPEED, 100, 30, 90,   -90.0f, 170.0f, 2500.0f, TURN_THRESHOLD_SS90E },  //  1 SS90ER 
    { (int)SEARCH_TURN_SPEED, 100, 30, 90,    90.0f, 170.0f, 2500.0f, TURN_THRESHOLD_SS90E },  //  2 SS90L   fast straight-to-straight 90
    { (int)SEARCH_TURN_SPEED, 100, 30, 90,   -90.0f, 170.0f, 2500.0f, TURN_THRESHOLD_SS90E },  //  3 SS90R  
    { (int)SEARCH_TURN_SPEED,  90, 30, 90,   180.0f, 191.0f, 2500.0f, TURN_THRESHOLD_SS90E },  //  4 SS180L  about-turn without stopping; R=90 lands one cell over
    { (int)SEARCH_TURN_SPEED,  90, 30, 90,  -180.0f, 191.0f, 2500.0f, TURN_THRESHOLD_SS90E },  //  5 SS180R 
    { (int)SEARCH_TURN_SPEED, 120, 30, 90,    45.0f,  95.0f, 2500.0f, TURN_THRESHOLD_SS90E },  //  6 SD45L   straight ONTO the diagonal - gentle, so a large R
    { (int)SEARCH_TURN_SPEED, 120, 30, 90,   -45.0f,  95.0f, 2500.0f, TURN_THRESHOLD_SS90E },  //  7 SD45R  
    { (int)SEARCH_TURN_SPEED, 120, 30, 90,    45.0f,  95.0f, 2500.0f, TURN_THRESHOLD_SS90E },  //  8 DS45L   diagonal back to straight - same angle, different geometry
    { (int)SEARCH_TURN_SPEED, 120, 30, 90,   -45.0f,  95.0f, 2500.0f, TURN_THRESHOLD_SS90E },  //  9 DS45R  
    { (int)SEARCH_TURN_SPEED,  90, 30, 90,   135.0f, 191.0f, 2500.0f, TURN_THRESHOLD_SS90E },  // 10 SD135L  straight onto the diagonal, the long way round
    { (int)SEARCH_TURN_SPEED,  90, 30, 90,  -135.0f, 191.0f, 2500.0f, TURN_THRESHOLD_SS90E },  // 11 SD135R 
    { (int)SEARCH_TURN_SPEED,  90, 30, 90,   135.0f, 191.0f, 2500.0f, TURN_THRESHOLD_SS90E },  // 12 DS135L  diagonal back to straight
    { (int)SEARCH_TURN_SPEED,  90, 30, 90,  -135.0f, 191.0f, 2500.0f, TURN_THRESHOLD_SS90E },  // 13 DS135R 
    { (int)SEARCH_TURN_SPEED,  63, 30, 63,    90.0f, 273.0f, 2500.0f, TURN_THRESHOLD_SS90E },  // 14 DD90L   corner WITHOUT leaving the diagonal; pitch is 127mm, so tight
    { (int)SEARCH_TURN_SPEED,  63, 30, 63,   -90.0f, 273.0f, 2500.0f, TURN_THRESHOLD_SS90E },  // 15 DD90R  
};

const char *const turn_names[TURN_COUNT] = {
    "SS90EL",
    "SS90ER",
    "SS90L",
    "SS90R",
    "SS180L",
    "SS180R",
    "SD45L",
    "SD45R",
    "DS45L",
    "DS45R",
    "SD135L",
    "SD135R",
    "DS135L",
    "DS135R",
    "DD90L",
    "DD90R",
};
