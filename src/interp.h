#ifndef INTERP_H
#define INTERP_H

#include <stdbool.h>

// Decoupled high-refresh presentation.
//
// The game logic keeps its fixed tick.  While the tick is waiting for its
// deadline, this module presents extra frames whose moving objects are
// linearly interpolated between the previous and the current tick's draw
// lists.  Everything here is presentation only: it never touches gameplay
// state, RNG or timing, and in Classic mode it reproduces the original path.

// "Smooth motion" setting (default on).  Display-only.
extern bool interp_smooth_motion;

// True when the smooth presentation loop applies to the current gameplay frame.
bool interp_active(void);

// Presents the current level gameplay frame.  Replaces the original
// wait_delay() + playfield-copy + JE_showVGA() sequence in JE_starShowVGA()
// and runs the interpolated presentation loop when active.
void interp_present_gameplay(void);

// Present the current gameplay frame from the live framebuffer, without the
// interpolation machinery (the original path).
void interp_present_live_frame(void);

#endif // INTERP_H
