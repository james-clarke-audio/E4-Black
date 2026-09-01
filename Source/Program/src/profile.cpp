/*
 * profile.cpp
 *
 * The two global motion profiles (forward + rotation). The Arduino build
 * defined these in the .ino; here they live in their own translation unit.
 */
#include "profile.h"

Profile forward;
Profile rotation;
