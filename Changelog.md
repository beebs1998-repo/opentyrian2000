# Changelog

## Audio
- Added an optional reverb/echo effect over the final mixed audio stream (music and sound effects together), toggleable at Setup - Sound - Reverb (ON/OFF). Off by default, so existing audio is unchanged until it is switched on.
  - Added `src/reverb.c`/`src/reverb.h` as a self-contained mono Schroeder reverberator (4 parallel feedback comb filters with one-pole lowpass damping into 2 series allpass filters, DC-normalized). Delay lengths are scaled to the sample rate that SDL actually negotiates.
  - `loudness.c` calls `reverb_init`/`reverb_process`/`reverb_deinit` around the existing mixer; the dry path is untouched, so no latency is added, and the reverb network always runs with a ~30 ms wet-gain ramp so toggling never clicks.
  - `varz.c`/`varz.h` gained `reverb_enabled` plus `applyReverb()`, mirroring the `small_hitbox_enabled`/`applySmallHitbox()` pattern.
  - `opentyr.c` added the `Setup - Sound - Reverb` ON/OFF picker row, following the existing picker-based option pattern.
  - `config.c` persists the setting as a new `[audio]` section key `reverb` in `opentyrian.cfg` (`on`/`off`); the binary `tyrian.cfg` layout is unchanged.
- Added an optional diffuse surround effect, toggleable at Setup - Sound - Surround (ON/OFF), off by default and separate from Stereo.
  - Stereo and Surround are mutually exclusive: both turn the mono mix into the channel pair, so there is nothing to chain one through the other. Turning either on turns the other off, in the menu and in `applyStereo()`.
  - `src/stereo.c` gained a mid/side output shape alongside the existing one-sided Haas one. The dry signal goes into *both* channels and a short ensemble of four decorrelated taps (1.5-7.5 ms, mixed-sign gains) supplies the side signal. Because both channels carry the onset undelayed, the precedence effect that makes Stereo lean left cannot occur, and the two channels sum back to the original signal.
  - Measured: the channels reconstruct the mono mix to within 1 LSB (`Stereo`/Haas is off by up to 21924 LSB from comb filtering), the inter-channel delay is 0 frames (`Stereo`/Haas at Wide is 264 frames), inter-channel correlation is 0.64, and per-channel level is within 0.62 dB of mono.
  - The cost is that each channel gets a mild comb from the tap ensemble, roughly 3 dB of ripple at the tap spacing. Unlike the Haas comb this is heard in stereo too. `SURROUND_GAIN` and the tap gains in `src/stereo.c` are the tuning constants.
  - `varz.c`/`varz.h` gained `surround_enabled`; `config.c` persists `[audio] surround`.
  - The Sound sub-menu now uses all 8 slots of its `items[8]` array.
- Added an optional stereo widening effect, toggleable at Setup - Sound - Stereo (ON/OFF), off by default and separate from Reverb.
  - The SDL audio device is now opened with 2 channels instead of 1. The channel count was never negotiable, so this is required to have left and right channels at all; the effect being off writes identical values to both, so the audio is unchanged until it is switched on.
  - `src/loudness.c` now mixes into a mono scratch buffer, because the OPL emulator, the SFX mixer and the reverb are all mono, and spreads it across the output channels at the very end. The existing mix, volume and reverb code is otherwise untouched.
  - Added `src/stereo.c`/`src/stereo.h`: a Haas-style widener that delays the upper spectrum in the right channel. Everything upstream is mono, so there is no side signal to amplify and the channels have to be decorrelated instead. The extreme bass is held back with a single one-pole lowpass so it stays common to both channels (measured side/dry 0.217 at 80 Hz versus 0.479 at 6 kHz). One section is deliberate: the widened signal is the complement of that lowpass, so stacking sections would raise the widened signal and defeat the purpose.
  - The delay line runs continuously with a ~30 ms crossfade on the right channel, so toggling cannot click, and at zero width the dry sample is copied directly so the off state is bit-identical to plain duplication.
  - Note that any mono-to-stereo widener comb-filters when downmixed to mono, so this is less clean on mono speakers. It is off by default for that reason.
  - Added `Setup - Sound - Stereo Width` (Narrow/Normal/Wide). The inter-channel delay is a straight tradeoff: a longer offset decorrelates tonal audio more and so sounds wider, but the ear also localises a sound towards whichever channel arrives first, so a wider setting leans the image further left. Measured inter-channel delay is 1.5, 3.0 and 6.0 ms for the three presets, and Normal is the default.
  - Changing the width mid-song cannot click: the crossfade is collapsed to zero first and the delay is swapped while both channels are bit-identical, so the swap is inaudible.
  - `varz.c`/`varz.h` gained `stereo_enabled`, `stereo_width` plus `applyStereo()`; `config.c` persists `[audio] stereo` and `[audio] stereo_width`.
  - The Sound sub-menu now uses all 7 slots of its `items[7]` array; any further Sound option needs that array widened.
  - `visualc/opentyrian.vcxproj` now lists `reverb.c`/`reverb.h` and `stereo.c`/`stereo.h`, and also the previously missing `drawlist.c`, `drawlist.h`, `interp.c`, and `interp.h` entries, so the MSVC project builds every source file.

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
