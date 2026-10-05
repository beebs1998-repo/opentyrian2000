/*
 * OpenTyrian2000: A modern cross-platform port of Tyrian
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
#include "interp.h"

#include "config.h"
#include "drawlist.h"
#include "keyboard.h"
#include "nortsong.h"
#include "network.h"
#include "opentyr.h"
#include "player.h"
#include "tyrian2.h"
#include "video.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

bool interp_smooth_motion = true;
static bool interp_vsync_known = false;
static bool interp_vsync_on = false;

bool interp_active(void)
{
	if (!interp_smooth_motion)
		return false;
	if (smoothScroll == 0)
		return false;
	if (playerEndLevel || skipStarShowVGA)
		return false;
	if (isNetworkGame)
		return false;
	return true;
}

static void interp_ensure_vsync(bool on)
{
	if (interp_vsync_known && interp_vsync_on == on)
		return;

	/* SDL_RenderSetVSync toggles presentation sync on the current renderer;
	 * when unavailable (or the backend ignores it), pacing falls back to the
	 * display-refresh soft delay in interp_present_gameplay(). */
	int applied = SDL_RenderSetVSync(main_window_renderer, on ? 1 : 0);
	interp_vsync_known = true;
	interp_vsync_on = applied == 0 && on;
}

// Fallback pacing interval (ms) from the actual display refresh.
static Uint32 interp_frame_interval(void)
{
	int display = SDL_GetWindowDisplayIndex(main_window);
	SDL_DisplayMode mode;

	if (SDL_GetCurrentDisplayMode(display, &mode) == 0 && mode.refresh_rate > 0)
		return 1000u / (Uint32)mode.refresh_rate;

	return 16;
}

static void interp_blit_playfield(SDL_Surface *game, int px, int py);

void interp_present_live_frame(void)
{
	int px = player[0].x;
	int py = player[0].y;

	if (drawlist_render_interpolated(65536))
	{
		drawlist_interpolated_player(&px, &py);
		interp_blit_playfield(drawlist_interpolated_game(), px, py);
		return;
	}

	// No previous list: raw game_screen with the same special-code mapping.
	interp_blit_playfield(game_screen, px, py);
}

static void interp_blit_playfield(SDL_Surface *game, int px, int py)
{
	JE_byte *src;
	Uint8 *s = VGAScreenSeg->pixels;
	int x, y, lightx, lighty, lightdist;

	src = game->pixels;
	src += 24;

	if (starShowVGASpecialCode == 1)
	{
		src += game->pitch * 183;
		for (y = 0; y < 184; y++)
		{
			memmove(s, src, 264);
			s += VGAScreenSeg->pitch;
			src -= game->pitch;
		}
	}
	else if (starShowVGASpecialCode == 2 && processorType >= 2)
	{
		lighty = 172 - py;
		lightx = 281 - px;

		for (y = 184; y; y--)
		{
			if (lighty > y)
			{
				for (x = 320 - 56; x; x--)
				{
					*s = (*src & 0xf0) | ((*src >> 2) & 0x03);
					s++;
					src++;
				}
			}
			else
			{
				for (x = 320 - 56; x; x--)
				{
					lightdist = abs(lightx - x) + lighty;
					if (lightdist < y)
						*s = *src;
					else if (lightdist - y <= 5)
						*s = (*src & 0xf0) | (((*src & 0x0f) + (3 * (5 - (lightdist - y)))) / 4);
					else
						*s = (*src & 0xf0) | ((*src & 0x0f) >> 2);
					s++;
					src++;
				}
			}
			s += 56 + VGAScreenSeg->pitch - 320;
			src += 56 + VGAScreenSeg->pitch - 320;
		}
	}
	else
	{
		for (y = 0; y < 184; y++)
		{
			memmove(s, src, 264);
			s += VGAScreenSeg->pitch;
			src += game->pitch;
		}
	}

	JE_showVGA();
}

// Renders the interpolated frame at `alpha_fx16` and presents it.  Falls back to
// the live tick frame when no previous list is available.
static void interp_render_and_present(Uint32 alpha_fx16)
{
	if (drawlist_render_interpolated(alpha_fx16))
	{
		int px, py;
		drawlist_interpolated_player(&px, &py);
		interp_blit_playfield(drawlist_interpolated_game(), px, py);
	}
	else
	{
		interp_blit_playfield(game_screen, player[0].x, player[0].y);
	}
}

void interp_present_gameplay(void)
{
	if (!interp_active())
	{
		// The original single-frame path.
		if (smoothScroll != 0)
		{
			wait_delay();
			setDelay(frameCountMax);
		}

		interp_blit_playfield(game_screen, player[0].x, player[0].y);
		return;
	}

	// Smooth path: present interpolated frames until the tick's deadline.
	const Uint32 deadline = getFrameDeadline();
	Uint32 period = getFramePeriod();
	if (period == 0)
		period = 1;
	const Uint32 start = deadline - period;

	interp_ensure_vsync(true);
	const bool paced_soft = !interp_vsync_on;
	const Uint32 interval = interp_frame_interval();

	for (;;)
	{
		Uint32 now = SDL_GetTicks();
		Sint32 elapsed = (Sint32)(now - start);
		Uint32 alpha_fx16;
		if (elapsed <= 0)
			alpha_fx16 = 0;
		else
			alpha_fx16 = (Uint32)(((Uint64)elapsed << 16) / period);
		if (alpha_fx16 > 65536u)
			alpha_fx16 = 65536u;

		interp_render_and_present(alpha_fx16);
		service_SDL_events(false);

		now = SDL_GetTicks();
		if ((Sint32)(now - deadline) >= 0)
			break;

		// Without vsync, pace to the display refresh instead of spinning.
		if (paced_soft)
		{
			const Uint32 remain = deadline - now;
			if (remain > interval)
				SDL_Delay(interval);
			else
				SDL_Delay(remain);
		}
	}

	/* Keep the scratch at the realised tick frame so the next tick, and any
	 * destination-reading filter, starts from it. */
	(void)drawlist_render_interpolated(65536);
	setDelay(frameCountMax);
}
