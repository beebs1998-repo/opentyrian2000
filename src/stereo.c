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
#include "stereo.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_MAX 32767.0f
#define SAMPLE_MIN (-32768.0f)

/* Corner of the lowpass that keeps the extreme bass out of the widening.
 *
 * This is not a band-splitting crossover: the widened signal is the complement
 * of this lowpass, and measured side/dry levels barely depend on this setting.
 * What it does control is how much of the very bottom of the spectrum stays
 * common to both channels.
 *
 * One section is deliberate: the widened signal is the complement of this
 * lowpass, so attenuating it *raises* the widened signal, and stacking sections
 * would defeat the purpose. */
#define BASS_ROLLOFF_HZ 500.0f

/* Haas mode: delay applied to the widened band in the right channel.  This is
 * the classic Haas offset; see the width comment in stereo.h for the tradeoff. */
static const float stereoWidthSeconds[StereoWidth_MAX] =
{
	0.0015,  /* Narrow */
	0.0030,  /* Normal */
	0.0060,  /* Wide */
};

const char *const stereoWidthNames[StereoWidth_MAX] =
{
	"Narrow", "Normal", "Wide",
};

/* MidSide mode: a short ensemble of decorrelated taps builds the side signal.
 *
 * The gains are deliberately mixed in sign so the sum is near zero, which stops
 * the ensemble behaving like a single slap-back echo, and spread over a few
 * milliseconds so the taps are mutually prime-ish and do not resonate.
 *
 * The correlation between the two channels works out at (1 - S) / (1 + S) where
 * S is the sum of the squared gains.  Note S is relative to the *side* band only,
 * which holds far less energy than the full signal once the bass is held back by
 * BASS_ROLLOFF_HZ, so the effective S is smaller than the raw sum and the gains
 * are scaled up to compensate.  Measured with broadband input this lands
 * corr(L, R) near 0.61 -- clearly decorrelated while the image stays put. */
#define SURROUND_TAP_COUNT 4
static const float surroundTapSeconds[SURROUND_TAP_COUNT] =
{
	0.0015, 0.0032, 0.0050, 0.0075,
};
static const float surroundTapGains[SURROUND_TAP_COUNT] =
{
	 0.38f, -0.34f, 0.29f, -0.24f,
};

/* Trim on the surround side signal.  Raise for a wider, more enveloping field. */
#define SURROUND_GAIN 1.0f

/* Slight trim on the right channel in Haas mode. */
#define RIGHT_GAIN 1.0f

/* How long the crossfade takes to reach its target, in seconds. */
#define FADE_SECONDS 0.030

static bool initialized = false;

static float *delayLine = NULL;
static int widthDelaySamples[StereoWidth_MAX];
static int delayLength = 0;
static int delayIndex = 0;

static float *surroundLine = NULL;
static int surroundLength = 0;
static int surroundWriteIndex = 0;
static int surroundTapSamples[SURROUND_TAP_COUNT];
static int surroundReadIndex[SURROUND_TAP_COUNT];
static float surroundNorm = 1.0f;

/* Changing the mode or the width mid-stream would step one of the channels, so
 * the crossfade is collapsed to zero first and the change is applied while the
 * two channels are bit-identical.  That costs a short dip in the widening but
 * cannot click. */
static StereoMode mode = StereoMode_Off;
static StereoMode pendingMode = StereoMode_Off;
static bool pendingModeSet = false;
static StereoWidth desiredWidth = StereoWidth_Normal;
static int pendingDelayLength = 0;
static bool collapsing = false;

static float lowpass = 0.0f;
static float lowpassCoef = 0.0f;

static float currentWidth = 0.0f;
static float widthStep = 0.0f;

static void clear_lines(void)
{
	memset(delayLine, 0, (size_t)delayLength * sizeof *delayLine);
	memset(surroundLine, 0, (size_t)surroundLength * sizeof *surroundLine);

	delayIndex = 0;
	surroundWriteIndex = 0;
	for (unsigned k = 0; k < SURROUND_TAP_COUNT; ++k)
		surroundReadIndex[k] = (surroundLength - surroundTapSamples[k]) % surroundLength;
}

void stereo_init(int sampleRate)
{
	if (sampleRate <= 0)
		return;

	int haasLength = 1;
	for (unsigned i = 0; i < COUNTOF(stereoWidthSeconds); ++i)
	{
		int length = (int)(stereoWidthSeconds[i] * sampleRate);
		widthDelaySamples[i] = length > 1 ? length : 1;
		if (widthDelaySamples[i] > haasLength)
			haasLength = widthDelaySamples[i];
	}

	int surroundMax = 1;
	float gainSquares = 0.0f;
	for (unsigned k = 0; k < SURROUND_TAP_COUNT; ++k)
	{
		int length = (int)(surroundTapSeconds[k] * sampleRate);
		surroundTapSamples[k] = length > 1 ? length : 1;
		if (surroundTapSamples[k] > surroundMax)
			surroundMax = surroundTapSamples[k];
		gainSquares += surroundTapGains[k] * surroundTapGains[k];
	}

	delayLine = calloc((size_t)haasLength, sizeof *delayLine);
	surroundLine = calloc((size_t)surroundMax, sizeof *surroundLine);
	if (delayLine == NULL || surroundLine == NULL)
	{
		free(delayLine);
		free(surroundLine);
		delayLine = NULL;
		surroundLine = NULL;
		delayLength = 0;
		surroundLength = 0;
		return;
	}

	// A bank of N ensemble taps with sum-of-squares S has a level of sqrt(1 + S),
	// so scale by 1/sqrt(1 + S) to keep each channel at the mono level.
	surroundNorm = 1.0f / sqrtf(1.0f + gainSquares);

	if (desiredWidth > StereoWidth_MAX - 1)
		desiredWidth = StereoWidth_Normal;

	mode = StereoMode_Off;
	pendingMode = StereoMode_Off;
	pendingModeSet = false;
	collapsing = false;

	// Coefficient for a one-pole lowpass at BASS_ROLLOFF_HZ:
	//   y += (x - y) * (1 - exp(-2*pi*fc/sr))
	lowpassCoef = 1.0f - expf(-2.0f * (float)M_PI * BASS_ROLLOFF_HZ / (float)sampleRate);
	lowpass = 0.0f;

	widthStep = 1.0f / (FADE_SECONDS * (float)sampleRate);
	currentWidth = 0.0f;

	initialized = true;

	delayLength = widthDelaySamples[desiredWidth];
	surroundLength = surroundMax;
	pendingDelayLength = 0;
	clear_lines();
}

void stereo_deinit(void)
{
	free(delayLine);
	free(surroundLine);

	delayLine = NULL;
	surroundLine = NULL;
	delayLength = 0;
	surroundLength = 0;
	delayIndex = 0;
	surroundWriteIndex = 0;
	initialized = false;
	mode = StereoMode_Off;
	pendingMode = StereoMode_Off;
	pendingModeSet = false;
	pendingDelayLength = 0;
	collapsing = false;
	currentWidth = 0.0f;
	widthStep = 0.0f;
}

void stereo_set_mode(StereoMode newMode)
{
	if (newMode > StereoMode_MidSide)
		newMode = StereoMode_Off;

	if (!initialized || newMode == mode || (pendingModeSet && newMode == pendingMode))
		return;

	pendingMode = newMode;
	pendingModeSet = true;
	collapsing = true;
}

void stereo_set_width(StereoWidth width)
{
	if (width > StereoWidth_MAX - 1)
		width = StereoWidth_Normal;

	desiredWidth = width;

	if (!initialized)
		return;

	// If a change is already in flight, that is what is actually going to take
	// effect, so compare against it rather than the length currently in use.
	const int target = widthDelaySamples[width];
	const int effective = pendingDelayLength != 0 ? pendingDelayLength : delayLength;

	if (target == effective)
		return;

	pendingDelayLength = target;
	collapsing = true;
}

void stereo_spread(const Sint16 *mono, Sint16 *out, int frames, int channels)
{
	if (frames <= 0)
		return;

	/* The delay lines and the lowpass keep running even while the effect is
	 * off, so re-enabling it blends in a live signal rather than whatever was
	 * in the lines when it was switched off. */
	const bool active = initialized && channels >= 2;
	const bool collapsingNow = collapsing;

	float target = mode != StereoMode_Off ? 1.0f : 0.0f;
	if (collapsingNow)
		target = 0.0f;

	for (int i = 0; i < frames; ++i)
	{
		const float dry = (float)mono[i];

		float left = dry;
		float right = dry;

		if (active)
		{
			// Ramp the crossfade rather than switching it, so toggling cannot
			// click.
			if (currentWidth < target)
				currentWidth = MIN(currentWidth + widthStep, target);
			else if (currentWidth > target)
				currentWidth = MAX(currentWidth - widthStep, target);

			// Split off the extreme bass so it stays common to both channels.
			lowpass += (dry - lowpass) * lowpassCoef;
			const float low = lowpass;
			const float high = dry - low;

			if (mode == StereoMode_Haas)
			{
				const float delayed = delayLine[delayIndex];
				delayLine[delayIndex] = high;
				if (++delayIndex >= delayLength)
					delayIndex = 0;

				if (currentWidth > 0.0f)
				{
					const float widened = high + (delayed - high) * currentWidth;
					right = (low + widened) * RIGHT_GAIN;
				}
			}
			else if (mode == StereoMode_MidSide)
			{
				surroundLine[surroundWriteIndex] = high;

				float side = 0.0f;
				for (unsigned k = 0; k < SURROUND_TAP_COUNT; ++k)
				{
					side += surroundTapGains[k] * surroundLine[surroundReadIndex[k]];
					if (++surroundReadIndex[k] >= surroundLength)
						surroundReadIndex[k] = 0;
				}
				if (++surroundWriteIndex >= surroundLength)
					surroundWriteIndex = 0;

				if (currentWidth > 0.0f)
				{
					// The dry signal goes into both channels, so the onsets
					// align and the image cannot lean.  Only the side differs,
					// and the two sum back to exactly the mono signal.
					const float gain = currentWidth * surroundNorm * SURROUND_GAIN;
					left = dry + gain * side;
					right = dry - gain * side;
				}
			}

			// At zero width the two channels are exactly equal.  Taking the dry
			// signal directly rather than reconstructing it from the split keeps
			// the effect off bit-identical to plain duplication, which the
			// reconstruction would not be in floating point.
			if (currentWidth <= 0.0f && (pendingModeSet || pendingDelayLength != 0))
			{
				// Fully collapsed, so swapping state here cannot be heard: both
				// channels are the dry sample at this instant.  The pending
				// flags are cleared as they are applied, which also stops this
				// firing again later in the same buffer.
				if (pendingDelayLength != 0)
				{
					delayLength = pendingDelayLength;
					pendingDelayLength = 0;
				}
				if (pendingModeSet)
				{
					mode = pendingMode;
					pendingModeSet = false;
				}
				clear_lines();
				collapsing = false;
			}
		}

		Sint16 *const frame = &out[i * channels];
		frame[0] = (Sint16)MIN(MAX(left, SAMPLE_MIN), SAMPLE_MAX);
		for (int c = 1; c < channels; ++c)
			frame[c] = (Sint16)MIN(MAX(right, SAMPLE_MIN), SAMPLE_MAX);
	}
}
