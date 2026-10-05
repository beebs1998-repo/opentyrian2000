# Changelog

## Build/Performance
- Replaced direct frame pacing with an interpolation-based 60 Hz presentation layer. The original logic tick rate is preserved; gameplay logic continues at the original cadence while the presented frame is interpolated/presented at the display refresh rate.
  - Added `src/drawlist.c`/`src/drawlist.h` as a per-tick recording layer for sprites, background rows, rectangles, fills, darken effects, filters, starfield, and superpixel passes into a tagged emitter, so replay/interpolation can be reconstructed.
  - Added `src/interp.c`/`src/interp.h` for decoupled presentation. presentation uses vsync pacing with a soft display-refresh fallback.
  - Wired recording contexts (`DL_OBJ_*`) through `tyrian2.c`, `mainint.c`, `shots.c`, `sprite.c`, `vga256d.c`, `backgrnd.c`, `varz.c`, `video.c`, and `drawlist.c`.
  - `nortsong.c/nortsong.h` gained frame deadline/period getters (`getFrameDeadline`, `getFramePeriod`).

## Controls/Movement
- Removed player ship inertia: ship acceleration towards target was removed in `src/mainint.c` (`accelXC += this_player->x - *mouseX_;` and `accelYC += ...` commented out), eliminating drift and ghosting ship motion.
- Keyboard/manual movement:
  - `CURRENT_KEY_SPEED` set to `5` in `src/varz.h`.
  - Added diagonal-aware handling in `src/mainint.c` and reduced diagonal speed via `CURRENT_KEY_SPEED_DIAGONAL` set to `4` in `src/varz.h`.

## Rendering/Interpolation Timing
- PIT/original delay logic restored to original cadence after an earlier 60 Hz presampler experiment was reverted; `nortsong` retains only deadline/period getters used by the interpolation layer.
