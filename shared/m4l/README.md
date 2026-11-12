# Max for Live devices (Push 3 standalone)

Every plugin has a Max for Live port in `plugins/<slug>/m4l/`, so it runs on Push 3 standalone (which can't load
VST3s). The devices are plain Max + gen~ with no externals, and they all share one UI kit, so they look like a
family regardless of each plugin's VST3 theme.

```
shared/m4l/
  aamax/            Python builder: params.py (Live parameters), ui.py (layout helpers), device.py (spec -> patcher),
                    patcher.py (Max JSON), amxd.py (frozen .amxd container)
  ui/aa_ui.js       the UI kit: theme, frame, knob, slider, segment, chips, stepper, toggle, button, action,
                    meter, label, readout (jsui + mgraphics, ES5)
  ui/preview/       browser stand-in for mgraphics (preview + smoke test only, never shipped)
  tests/            test_aamax.py (+ smoke.js, run under node)
plugins/<slug>/m4l/
  device.py         the spec: DEVICE = Device(...)   (params, Push banks, layout, gen~ file, monitors)
  <slug>.genexpr    the DSP (a GenExpr port of Source/dsp)
  ui.js             the device's own widgets (optional), e.g. Duck's curve editor
  <Name>.amxd       built, checked in (frozen: the UI script is embedded)
scripts/build_m4l.py
```

## Build, check, preview

```sh
python3 scripts/build_m4l.py                 # build every device (or: build_m4l.py duck cosmos)
python3 scripts/build_m4l.py --check         # CI: fail if a checked-in .amxd is stale
python3 scripts/build_m4l.py --preview       # + build/m4l-preview/index.html: every UI, live, in a browser
python3 -m unittest discover -s shared/m4l/tests
python3 scripts/check_m4l_gen.py             # macOS + Live installed: compile every gen~ with the real Max compiler
```

The tests build every device, decode the container, check parameters / banks / routing / gen~ `Param`s against the
spec, lint the UI scripts for ES5, and boot each UI under node to paint and click across the whole device
(`smoke.js`). `scripts/check_m4l_gen.py` then compiles each device's gen~ in the Max that ships inside Live (a
separate headless process; it starts DSP, dumps the Max console, quits). Run it before shipping any GenExpr change:
the builder's lint catches known mistakes, but only Max's compiler is authoritative. It can't check how it sounds, so
still load the device in Live.

## How a device is wired

```
hidden Live parameter objects --(prepend <id>)-----------------------> gen~ (the DSP)
       |  ^                                                              |
       |  +--(route <ids> gen ext)-- jsui (the whole UI) <-(mon <name>)--+ snapshot~ / peakamp~
       +---------------(prepend param <id>)--------^
```

- **Parameters** are hidden `live.numbox` (dials, choices, toggles), `live.text` (buttons) or `pattr` (blobs)
  objects. Push, automation, MIDI mapping and Live sets all see these, never the UI. The `Device.banks` list is the
  Push bank layout (8 per bank; every automatable parameter must be in one).
- **The UI is one `jsui`** covering the device. It draws everything and edits parameters by sending
  `<id> <value>`. It never makes sound: anything audible goes through a parameter or gen~, so the device behaves
  the same on Push, which doesn't show device UIs.
- **Monitors** are extra gen~ outlets sampled for the UI (`Monitor("phase", ms=16)`, `mode="peak"` for levels).
  Output meters (`mon peakl` / `peakr`) are added when `meters=True`. With `transport=True` the UI also gets
  `mon playing 0|1`.
- **Device-specific wiring** (MIDI in/out, `js` helpers) goes in `Device.extra(ctx)`; see `Context` in device.py.

## Parameters (`aamax.params`)

| Class | Live object | UI value | gen~ gets |
| --- | --- | --- | --- |
| `Dial(name, id, lo, hi, default, unit, centre=None, bipolar=False)` | live.numbox float | real value | `<id> v` |
| `Choice(name, id, items, default)` | live.numbox enum | index | `<id> i` |
| `Toggle(name, id, default)` | live.numbox off/on | 0/1 | `<id> 0/1` |
| `Button(name, id)` | live.text (momentary) | press | `<id> n` (a counter: a change = a press) |
| `Blob(name, id, size, default, to_gen)` | pattr (blob) | list | `<id>0 v0` .. `<id>{size-1} v` |

`name` is the Live long name: never rename it once shipped (sets store values by name). `id` is a lowercase word
used as the gen~ `Param` name and the UI's key. Units: `%`, `Hz`, `dB`, `ms`, `st`, `int`, `float`, or a printf
format (`"%.1f s"`). `centre` skews a dial so its middle lands there. Set `to_gen=False` for parameters only
device wiring reads.

## Design rules (keep the family consistent)

- 169 px tall (Live's device strip). The kit draws the background and a 26 px header (name, `tagline`,
  ANIMAL AUDIO). Content runs from y=32 to y=163, 8 px outer margins, 6 px between cards.
- Group controls in `ui.card(rect, title)`. Knobs are `ui.knob(id, x, y)` (36 px dial in a 52 px cell, label and
  value below); rows via `ui.knob_row`. Choices: `ui.segment` (≤ ~6 short items), `ui.chips` (grids),
  `ui.stepper` (long lists). Toggles and buttons are pills; `ui.action` runs a UI-only command.
- Colour comes only from `AA.theme` / tones: `accent` (default: cyan to violet gradient), `warm` (hold, latch,
  freeze), `hot` (record), `good` (status, sync). No per-device palettes or literals in device ui.js files.
- Custom widgets (`ui.widget("<slug>.<name>", rect, ...)`, drawn in the device's `ui.js`) use the kit's helpers
  (`AA.fill`, `AA.stroke`, `AA.fillGradient`, `AA.text`, `AA.spaced`, `AA.line`, `AA.circle`, `AA.drawArc`) and sit
  on a `t.well` inset like Duck's curve. Define `AA.demo(seconds)` to fake monitor values for the preview.

## UI kit API (aa_ui.js)

```js
AA.widget("duck.curve", {
    paint: function (g, w) { /* g = mgraphics; w.rect = [x, y, w, h] in device px; w.spec = the layout dict */ },
    down: function (w, x, y, mods) {}, drag: function (w, x, y, mods) {}, up: function (w, x, y) {},
    dbl: function (w, x, y) {}, wheel: function (w, dy) {}, move: function (w, x, y) {},  // x, y local to w.rect
    tick: function (w) { return changed; },  // every 33 ms; return true to repaint
    interactive: false                       // for display-only widgets
});
AA.values[id]           // current parameter values (blobs: arrays)
AA.set(id, value)       // edit a parameter (blob: AA.set(id, [..]))
AA.monitor(name, dflt)  // latest `mon` value from the DSP
AA.watch(id, fn)        // fn(value) whenever a parameter changes, from anywhere
AA.gen(name, ...)       // message straight to gen~ (not saved, not on Push: prefer parameters)
AA.ext(name, ...)       // message to the device's extra wiring (route ... ext)
AA.actions[name] = fn   // for ui.action buttons
AA.format(id, v), AA.norm(id, v), AA.denorm(id, x), AA.dev.data (Device.ui_data)
```

Max's `jsui` runs an old JavaScript engine: write ES5 only (`var`, `function`; no arrows, `let`/`const`,
classes, template strings, spread, `Array.from`, `.includes`, `.find`). The tests enforce it.

## gen~ rules

Each of these caused silence or a dead feature in Max even though the code looked fine:

- Never pass a user function's result straight into `poke()` (or `Delay.write()`): assign it to a variable first.
  gen~'s type pass never types a user function nested inside `poke()` and code generation fails
  (`gen2.dsp.Model:1455: assertion failed`): the whole device goes silent.
- GenExpr has no `sr`: write `sr = samplerate;` before using it ("variable sr is not defined"). The builder rejects
  any identifier that is never defined.
- Don't pass `peek(...)` straight into a user function that returns a ternary; read it into a variable first
  ("variable peek_N is not defined"). The builder checks.
- Never name a `Param`, `History`, `Data`, `Buffer`, `Delay` or function like a gen~ operator (`mix`, `cross`,
  `phase`...; list in `aamax/gen_operators.txt`). The builder checks.
- Give every `Param` an explicit `min` and `max` (the builder checks). gen~ defaults a missing range to 0..1 and
  clamps to it.
- Keep integer literals below 2^31 (emitted as C `int`).
- Don't pre-declare a flag with a constant and set it inside blocks (`ticked = 0; if (a) { ticked = 1; } if
  (ticked) ...`): constant folding can treat it as always false. Compute it in one expression instead.
- Parenthesize any expression chaining 3+ operators of mixed precedence, e.g. `(a > b) && ((c - d) < e)`.
- No user function calls another or returns multiple values; lookup tables live in `Data` filled on the first
  sample (or generated by `Device.gen_inserts`: a `// @insert <key>` line in the GenExpr is replaced at build time,
  which is how Duck takes its presets straight from the C++ source).
- `in1..inN` / `out1..outM` must match the device: `gen_inputs` audio inlets (+1 for the beat ramp when
  `transport=True`), `gen_outputs` audio outlets, then one outlet per `Monitor`. The builder checks.
- With `transport=True`, declare `Param hostplaying(0, min=0, max=1)`, `hostbpm(0, min=0, max=1000)`,
  `hostbeats(0, min=-1000000, max=1000000000)`. The beat ramp (plugphasor~) is the last inlet; the host beat
  position is `round(hostbeats - ramp) + ramp`, `hostbpm` is Live's tempo in BPM (plugsync~ outlet 5), so a beat is `60 * sr / hostbpm` samples.

## Adding a device

1. `plugins/<slug>/m4l/device.py` with `DEVICE = Device(...)` (copy the shape of `plugins/duck/m4l/device.py`).
2. `<slug>.genexpr` ported from `Source/dsp`; optional `ui.js` for custom widgets.
3. `python3 scripts/build_m4l.py <slug> --preview`, look at the preview, run the tests, load it in Live.
4. A short `plugins/<slug>/m4l/README.md`: Push banks and differences from the VST3.
