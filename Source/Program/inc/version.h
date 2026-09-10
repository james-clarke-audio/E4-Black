/*
 * version.h — firmware build identity, shown on the OLED at boot and in
 * Diagnostics > Firmware ver, and streamed over BT as "VER,<ver>,<build>".
 *
 * Bump FW_VERSION by hand at each meaningful milestone. FW_BUILD is stamped
 * automatically by the compiler; it refreshes whenever app_main.cpp is
 * recompiled, so do a clean build if you need the timestamp to be exact.
 */
#ifndef VERSION_H_
#define VERSION_H_

#define FW_VERSION "0.8"
#define FW_BUILD   (__DATE__ " " __TIME__)

#endif /* VERSION_H_ */
