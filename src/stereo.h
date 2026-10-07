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
#ifndef STEREO_H
#define STEREO_H

#include "opentyr.h"

#include "SDL.h"

/* Widens a mono signal into an interleaved stereo signal.
 *
 * Everything upstream of this is mono (the OPL2 music bus and the .snd samples
 * are both single channel), so there is no side signal to amplify -- the left
 * and right channels have to be decorrelated instead.
 *
 * Two shapes of the same idea are available, because both take the mono mix
 * and produce the channel pair there is nothing left to chain:
 *
 *   StereoMode_Haas     the classic one-sided widener.  The left channel is the
 *                       mono signal untouched and the upper spectrum is delayed
 *                       in the right channel.  Sounds wide, but because the ear
 *                       localises a sound towards whichever channel arrives
 *                       first, the image leans left.  Also comb-filters when
 *                       downmixed to mono.
 *
 *   StereoMode_MidSide  puts the dry signal in *both* channels and adds a
 *                       decorrelated ensemble as the side signal.  Both channels
 *                       therefore carry the onset undelayed, so it cannot lean,
 *                       and the channels sum back to exactly the original, so
 *                       the mono downmix is clean.  Costs each channel a mild
 *                       comb from the ensemble, which is heard in stereo too.
 *
 * In both modes the dry path costs nothing extra and the delay lines run
 * continuously even while the effect is off, so toggling cannot click or
 * replay a stale delay.  At zero width the dry sample is copied directly, so
 * the effect is off is bit-identical to plain duplication.
 */

typedef enum
{
	StereoMode_Off = 0,
	StereoMode_Haas,
	StereoMode_MidSide,
} StereoMode;

/* How far the two channels are offset in Haas mode.  This is a straight
 * tradeoff: a longer offset decorrelates tonal audio more (so it sounds wider)
 * but also shifts the image further to the leading channel. */
typedef enum
{
	StereoWidth_Narrow = 0,
	StereoWidth_Normal,
	StereoWidth_Wide,
	StereoWidth_MAX
} StereoWidth;

extern const char *const stereoWidthNames[StereoWidth_MAX];

void stereo_init(int sampleRate);
void stereo_deinit(void);

void stereo_set_mode(StereoMode mode);
void stereo_set_width(StereoWidth width);

/* Converts `frames` mono samples into `channels` interleaved samples per frame.
 * Anything below two channels is a plain duplication. */
void stereo_spread(const Sint16 *mono, Sint16 *out, int frames, int channels);

#endif /* STEREO_H */
