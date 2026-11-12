# Turtle Time for Max for Live

`TurtleTime.amxd` is the Turtle Time time warp (half-time, slow, reverse, beat repeat) as a Max for Live audio
effect. It's plain Max + gen~ with no externals, so it runs on Push 3 standalone. Built from `device.py`,
`turtletime.genexpr` and `ui.js` with the shared kit in `shared/m4l` (`python3 scripts/build_m4l.py turtletime`).
The pattern table and names are read from `Source/dsp/TimePatterns.h` at build time.

## Use

Drop `TurtleTime.amxd` on an audio track (in Live, or copy it to Push's User Library). Press play: the effect locks
to Live's beat position. While Live is stopped the audio passes dry; the next play snaps back in on the beat.
Pick a pattern in the matrix (rows HALF / SLOW / REVERSE / REPEAT, columns 1/32 to 2 bars), set MIX, and choose
PITCH DROP (tape) or KEEP (time-stretch, slowed patterns only). On Push, the controls are in one bank:

| Bank | 1 | 2 | 3 |
| --- | --- | --- | --- |
| Turtle Time | Pattern | Mix | Keep Pitch |

The parameter long names match the VST3 (`Pattern`, `Mix`, `Keep Pitch`). Mix runs 0..100 % here (0..1 in the VST3).

## Differences from the VST3

- History is 2^20 samples (21.8 s at 48 kHz, 23.8 s at 44.1 kHz) instead of 32 s. The longest look-back is 16
  beats (Reverse 2 Bar, Repeat 2 Bar), so those two need at least 44 BPM at 48 kHz (41 BPM at 44.1 kHz); every
  other pattern works down to about 22 BPM. Below that they read audio that's already been overwritten.
- Always stereo. The grain aligner compares an L+R history channel instead of summing two reads.
- The beat position comes from `plugphasor~` (sample-accurate) plus `plugsync~` (polled every 10 ms), not the
  VST3 playhead. The play/stop state is polled too, so a start or stop can land up to 10 ms late.
- The scope's envelopes are binned in the UI from 33 ms peak monitors, so they're coarser than the plugin's
  192-bin `CycleScope`. They clear on a pattern change and freeze while stopped, like the plugin's.
