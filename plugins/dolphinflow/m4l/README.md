# Dolphin Flow for Max for Live

`DolphinFlow.amxd` is a Max for Live **instrument** port of the Dolphin Flow VST3. It uses plain Max, gen~ and
Max's built-in `js` (no externals), so it runs on Push 3 standalone. Built from this folder by
`python3 scripts/build_m4l.py dolphinflow`; the framework is described in `shared/m4l/README.md`.

## Playing it (Push 3, or any MIDI)

Put it on a MIDI track and play the pads or keys. A single note is the **root**: CHORD picks the quality and the
voices glide to the nearest notes of the new chord (voice leading). When you hold several roots, the newest one wins,
and letting it go falls back to the one still held. Three or more notes held together are played exactly as voiced,
so a recorded chord clip plays back the same way. The octave you play in moves the register, on top of OCTAVE.
LATCH holds the chord after you let go.

**LOOP** plays the 8 arrangement slots in order. It follows Live's beat position while Live plays, and otherwise
free-runs at Live's tempo from the moment LOOP is switched on. Empty slots are skipped. While the loop plays, your
keys are tracked but the loop owns the sound. When LOOP stops, any held keys take over and glide on from the loop's
last chord. Every slot is a Live parameter, so the progression can be edited from Push.

**Write Clip** (CLIP in the UI, Sound bank on Push) writes one pass of the loop, voiced the way it plays, into the
first empty clip slot of the device's track, using the Live API. If the device sits inside a rack, it walks up to the
track. The UI briefly shows the result: the scene number, NO EMPTY SLOT, or NOTHING TO WRITE.

In the device UI (desktop), click a slot to arm it: the next chord you play is written into it, and the next slot
arms. Drag up or down on a slot to change its root, Option- or Shift-drag to change its quality, click the - / +
corners to change its length, and double-click to empty it.

## Push banks

| Bank | Parameters |
| --- | --- |
| Play | Chord, Mode, Voices, Octave, Glide, Curve, Latch, Loop |
| Sound | Tone, Width, Space, Level, Write Clip |
| Roots | Slot 1..8 Root (Off, C..B) |
| Chords | Slot 1..8 Chord |
| Lengths | Slot 1..8 Length (1 beat .. 4 bars) |

## How it is split

- `midiin` -> `midiparse` -> **`logic.js`** (`js`, ES5): the held-root stack, chord shapes and the voice leader (the
  same search and costs as `VoiceLeader.h`, in exact integer units), chord naming, the loop's settled voicings and
  Write Clip. It runs only at note events and parameter changes and sends gen~ a voicing (`n0..n5`, `nv`, `gon`, then
  `trig`) and the loop table (`t<k>len`, `t<k>n<j>`, `lv`, then `tver`). gen~ only applies a voicing or a table when
  the last message (`trig` / `tver`) changes, so a half-sent update is never heard.
- **`dolphinflow.genexpr`** (gen~): the per-sample work. It handles the envelope per mode, a constant-time glide shaped
  by CURVE, 6 voices of 2 PolyBLEP saws, equal-power pan by WIDTH, the shared SVF low-pass (TONE plus the mode's
  filter envelope), the 8-line FDN reverb (SPACE) and LEVEL. It also runs the **loop's timing**, so slot changes are
  sample-accurate and locked to Live's beat. It reports `active` and the playing slot back to `logic.js`.
- `ui.js`: the flow display (chord name, voiced notes, voice-pitch ribbons), the glide and envelope previews, the
  slots and the Write Clip status. Nothing audible depends on the UI.
- `test_logic.js`: `node plugins/dolphinflow/m4l/test_logic.js` checks the logic against cases from the C++ tests.
- `device.py` reads the chord shapes and slot lengths from `Source/dsp`. The build fails if `logic.js`'s copy of
  those tables drifts.

**Timing.** Max's `js` runs in the low-priority thread, so live chord changes can arrive a few milliseconds late
(more if Max's main thread is busy). Notes that arrive together are also collected for 1 ms, so a chord is one glide
rather than three. gen~ applies the result at the next audio block. The loop doesn't have this delay: it is timed
inside gen~. The worst-case voice-leading search (6 voices, MIN9) takes about 0.2 ms under node and is probably 10
to 50x slower in Max's `js`.

## Differences from the VST3

- **Dropped:** MPE out, the virtual MIDI port, EchoGuard and MIDI out. An M4L instrument can't send MIDI to other
  tracks, and these only worked around VST3 limits. Use Write Clip to get the notes into a clip.
- **Dropped:** the on-screen root keyboard and computer-key shortcuts. Play from Push, a keyboard, or Live's own
  computer MIDI keyboard.
- **Replaced:** the drag-a-MIDI-clip export is now Write Clip (Live API) into the first empty clip slot.
- **Changed:** the arrangement is 24 Live parameters instead of plugin state, so Push can edit it. OCTAVE is a
  choice (-2..+2) rather than an integer.
- **Changed:** the loop always plays the voicings of a settled second pass (as the VST3's clip export does). The
  VST3 keeps voice-leading live after its first chord. The loop's register is OCTAVE alone (not the played octave).
- **Changed:** pitch, filter and pan update every sample instead of every 8. Voice-leading ties are broken in exact
  arithmetic (the C++ sums floats); 12,484 random progressions matched the C++ exactly.
- **Changed:** MIDI channel is ignored, so the same note on two channels is one key.
