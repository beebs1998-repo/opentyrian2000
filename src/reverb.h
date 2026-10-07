/* 
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef REVERB_H
#define REVERB_H

#include "opentyr.h"

#include "SDL.h"

/* A Schroeder reverberator (parallel comb filters into series allpass
 * filters), used as a post-mix effect on the final audio stream.
 *
 * The dry signal passes through untouched, so enabling the effect adds no
 * latency.  The reverb network itself runs continuously, even while the
 * effect is switched off, so that the delay lines always hold a current
 * signal; only the wet gain is ramped.  That makes toggling click-free and
 * avoids replaying a stale tail.
 */

void reverb_init(int sampleRate);
void reverb_deinit(void);

void reverb_set_enabled(bool enabled);
void reverb_process(Sint16 *samples, int count);

#endif /* REVERB_H */
