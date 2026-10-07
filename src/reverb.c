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
#include "reverb.h"

#include <stdlib.h>

#define COMB_COUNT 4
#define ALLPASS_COUNT 2

/* Delay lengths in samples at 44100 Hz, taken from Freeverb's tuning.  They are
 * close to, but not exactly, mutually prime, which keeps the tails from
 * building up a strong resonance. */
static const int combTuning[COMB_COUNT] = { 1116, 1188, 1277, 1356 };
static const int allpassTuning[ALLPASS_COUNT] = { 556, 225 };

#define REFERENCE_SAMPLE_RATE 44100

/* Comb feedback, which sets the decay time.  A comb falls 60 dB after
 * ln(1000) / ln(1/g) round trips through its delay line, so at g = 0.84 that
 * is 39 round trips: 1.00 s for the shortest comb above and 1.22 s for the
 * longest, giving the bank as a whole an effective decay of about 1.0 s --
 * a medium room.  This must stay below 1.0 or the network rings forever. */
static const float combFeedback = 0.84f;

/* One-pole lowpass in each comb's feedback path.  Larger values darken the
 * tail by rolling off its high frequencies. */
static const float combDamping = 0.35f;

/* Allpass feedback; the allpass filters are unity gain, so this only colours
 * the tail. */
static const float allpassFeedback = 0.5f;

/* Wet/dry balance.  Reverb adds energy on top of an already hot mix that the
 * mixer clamps hard, so stay conservative here. */
static const float wetGain = 0.30f;

/* How long the wet gain takes to reach its target, in seconds. */
static const float wetGainRampSeconds = 0.030f;

typedef struct
{
	float *samples;
	int length;
	int index;
	float store;
} Filter;

static Filter combs[COMB_COUNT];
static Filter allpasses[ALLPASS_COUNT];

static bool initialized = false;
static bool enabled = false;
static float combScale = 0.0f;
static float wetGainStep = 0.0f;
static float currentWetGain = 0.0f;

static bool filter_init(Filter *filter, int tuning, int sampleRate)
{
	int length = (int)(((long)tuning * sampleRate + REFERENCE_SAMPLE_RATE / 2) / REFERENCE_SAMPLE_RATE);
	if (length < 1)
		length = 1;

	filter->samples = calloc((size_t)length, sizeof *filter->samples);
	if (filter->samples == NULL)
		return false;

	filter->length = length;
	filter->index = 0;
	filter->store = 0.0f;

	return true;
}

static void filter_deinit(Filter *filter)
{
	free(filter->samples);

	filter->samples = NULL;
	filter->length = 0;
	filter->index = 0;
	filter->store = 0.0f;
}

/* Feedback comb filter: the delay line feeds back into itself through a
 * one-pole lowpass, so the tail decays and darkens over time. */
static float filter_comb(Filter *filter, float input)
{
	const float output = filter->samples[filter->index];

	filter->store = filter->store * combDamping + output * (1.0f - combDamping);
	filter->samples[filter->index] = input + filter->store * combFeedback;

	if (++filter->index >= filter->length)
		filter->index = 0;

	return output;
}

static float filter_allpass(Filter *filter, float input)
{
	const float output = filter->samples[filter->index];

	filter->samples[filter->index] = input + output * allpassFeedback;

	if (++filter->index >= filter->length)
		filter->index = 0;

	return output - input;
}

void reverb_init(int sampleRate)
{
	if (sampleRate <= 0)
		return;

	for (size_t i = 0; i < COUNTOF(combs); ++i)
	{
		if (!filter_init(&combs[i], combTuning[i], sampleRate))
		{
			reverb_deinit();
			return;
		}
	}

	for (size_t i = 0; i < COUNTOF(allpasses); ++i)
	{
		if (!filter_init(&allpasses[i], allpassTuning[i], sampleRate))
		{
			reverb_deinit();
			return;
		}
	}

	// A bank of N parallel combs with feedback g has a DC gain of N / (1 - g),
	// so scale by (1 - g) / N to keep the wet signal at unity level.
	combScale = (1.0f - combFeedback) / COMB_COUNT;

	wetGainStep = 1.0f / (wetGainRampSeconds * (float)sampleRate);
	currentWetGain = enabled ? wetGain : 0.0f;

	initialized = true;
}

void reverb_deinit(void)
{
	for (size_t i = 0; i < COUNTOF(combs); ++i)
		filter_deinit(&combs[i]);

	for (size_t i = 0; i < COUNTOF(allpasses); ++i)
		filter_deinit(&allpasses[i]);

	initialized = false;
	combScale = 0.0f;
	wetGainStep = 0.0f;
	currentWetGain = 0.0f;
}

void reverb_set_enabled(bool enable)
{
	enabled = enable;
}

void reverb_process(Sint16 *samples, int count)
{
	if (!initialized || count <= 0)
		return;

	const float target = enabled ? wetGain : 0.0f;

	for (int i = 0; i < count; ++i)
	{
		const float dry = (float)samples[i];

		float wet = 0.0f;
		for (size_t j = 0; j < COMB_COUNT; ++j)
			wet += filter_comb(&combs[j], dry);

		wet *= combScale;

		for (size_t j = 0; j < ALLPASS_COUNT; ++j)
			wet = filter_allpass(&allpasses[j], wet);

		// Ramp the wet gain rather than switching it, so that toggling the
		// effect mid-song doesn't click.
		if (currentWetGain < target)
			currentWetGain = MIN(currentWetGain + wetGainStep, target);
		else if (currentWetGain > target)
			currentWetGain = MAX(currentWetGain - wetGainStep, target);

		const float sample = dry + wet * currentWetGain;
		samples[i] = (Sint16)MIN(MAX(sample, -32768.0f), 32767.0f);
	}
}
