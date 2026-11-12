# COSMOS for Max for Live (Push 3 standalone)

`Cosmos.amxd` is a Max for Live audio effect port of the Cosmos VST3. Push 3 standalone can't load VST3s, but it
can run Max for Live devices made of plain Max + gen~. This one has no externals; it's built on the shared M4L
framework (`shared/m4l`), so it looks like the rest of the Animal Audio devices rather than the VST3.

| File | What |
| --- | --- |
| `cosmos.genexpr` | The DSP. A GenExpr port of `Source/dsp` (CosmosEngine, PhraseLooper) plus the shared Svf and FdnReverb |
| `device.py` | The spec: Live parameters, Push banks, UI layout, gen~ monitors (looper state, position, length) |
| `ui.js` | Cosmos's own widgets: mode family readout, looper display, REC/PLAY/DUB button |
| `Cosmos.amxd` | The built device (checked in so you can use it without building) |

## Use it

1. Drag `Cosmos.amxd` onto an audio track in Live (desktop) or copy it into your User Library
   (`Presets/Audio Effects/Max Audio Effect`).
2. To use it on Push 3 standalone, transfer it like any other Set content: save a Set that uses Cosmos and send
   it to Push, or copy the device to Push's User Library from Live's browser while Push is connected.

## Push controls

Every control is a Live parameter, so Push's encoders and display pick them up. The banks are:

| Bank | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Cosmos | Mode | Variation | Activity | Shape | Filter | Resonance | Mix | Hold |
| Time + FX | Time | Sync | Tempo | Repeats | Mod Freq | Mod Depth | Space | Reverb Time |
| Looper | Rec/Play | Stop | Clear | Loop Level | Loop Rev | Pre-FX | FX Vol | |

REC/PLAY works like the plugin's looper button: REC, then PLAY, then DUB, then back to PLAY. On the desktop the
button shows what the next press does.

## Differences from the VST3

- 24 grains instead of 48, to save CPU on Push. Dense HAZE / STRUM settings drop the extra grains.
- 2^20-sample history: about 21 s at 48 kHz, 11 s at 96 kHz. The looper holds 60 s at 48 kHz.
- When SYNC is on, the clock follows Live's tempo and locks to the beat through `plugphasor~` and `plugsync~`.
  There is no TAP button. With SYNC off, use the Tempo knob.
- In POST-FX looper mode, the looper hears the output one sample late.
- The random sequences differ from the plugin's, so renders are not sample-identical.
- The desktop device view shows the looper state, position and length. Push only shows the parameters.

## Change it

Edit `cosmos.genexpr`, `device.py` or `ui.js`, then rebuild with `scripts/build_m4l.py` (`python3
scripts/build_m4l.py cosmos --preview` to also see the UI in a browser). Never rename a Live parameter: sets store
values by name. Keep the GenExpr in step with `Source/dsp` when the engine changes, and follow the gen~ rules in
[shared/m4l/README.md](../../../shared/m4l/README.md): each of them once silenced this device in Max.
