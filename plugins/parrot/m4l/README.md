# PARROT for Max for Live (Push 3 standalone)

`Parrot.amxd` is a Max for Live audio effect port of the Parrot VST3: it moves a voice's pitch and formants
independently (SHIFT / SNAP / ROBOT) and drives it through a two-stage 12AX7 model. Plain Max + gen~, no
externals, so it runs on Push 3 standalone. It uses the shared kit in `shared/m4l` (see its README).

| File | What |
| --- | --- |
| `parrot.genexpr` | The DSP: a GenExpr port of `Source/dsp` (ParrotEngine, VoiceShifter) and the shared PitchTracker and TubeDrive |
| `device.py` | Parameters, Push banks, layout, MIDI wiring, latency |
| `ui.js` | Pitch trace, mode status, target / interval readout, tube heat |
| `Parrot.amxd` | The built device (`python3 scripts/build_m4l.py parrot`) |

## Use it

Drop `Parrot.amxd` on an audio track (Live, or Push 3 standalone via a Set or your User Library). It is monophonic:
one voice in, not chords. It reports 3306 samples of latency (69 ms at 48 kHz), so Live keeps it in time.

ROBOT holds one note: ROBOT NOTE, or the MIDI key held. For MIDI, route a MIDI track's "MIDI To" to the Parrot
track and pick Parrot as the target. The newest held key wins and releasing it falls back to the one still held.

## Push banks

| Bank | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Parrot | Pitch | Formant | Mode | Robot Note | Glide | Drive | Mix | Link |
| Options | Fine | Link | Mode | Robot Note | Glide | | | |

Parameter names match the VST3 (Pitch, Formant, Fine, Link, Mode, Robot Note, Glide, Drive, Mix).

## Differences from the VST3

- Grain reads are 4-point cubic instead of the windowed-sinc reader. Formant-up grains read a copy of the input
  low-passed at 0.47 x sample rate / formant ratio, the cutoff the sinc used. At neutral settings the output is
  still the input, delayed (cubic is exact on whole samples).
- 24 grain slots (the VST3 has 128; at most 16 overlap, at a 4x ratio and -12 st formant).
- The drive is the same 12AX7 model (Koren plate curves, grid conduction, cathode bias, coupling), oversampled 2x
  with a 47-tap halfband instead of 4x with 129 taps. Its tables are solved in the first ~50 ms after load, and
  DRIVE stays clean until then.
- Latency is one fixed budget, because Live's Defined Latency is a fixed number of samples: 3283 samples for the
  shifter plus 23 for the drive. 44.1 and 48 kHz match the VST3. Above 48 kHz the lowest tracked pitch rises so
  the design still fits: about 158 Hz at 96 kHz, so low voices pass through unshifted there.
- The held-note stack is unlimited (the VST3 keeps 16).

Checked against the VST3 engine on synthetic voices (75-600 Hz, every mode, +-12 st pitch and formant, drive):
the same output pitch, level and formant shift. Not yet run in Max itself.

## Change it

Edit `parrot.genexpr`, `device.py` or `ui.js`, then `python3 scripts/build_m4l.py parrot --preview` and
`python3 -m unittest discover -s shared/m4l/tests`. Keep the GenExpr in step with `Source/dsp` and follow the gen~
rules in `shared/m4l/README.md`.
