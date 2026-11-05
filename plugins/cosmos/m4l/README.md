# COSMOS for Max for Live (Push 3 standalone)

`Cosmos.amxd` is a Max for Live audio effect port of the Cosmos VST3. Push 3 standalone can't load VST3s, but it
can run Max for Live devices made of plain Max + gen~. This one has no externals and no JavaScript.

| File | What |
| --- | --- |
| `cosmos.genexpr` | The DSP. A GenExpr port of `Source/dsp` (CosmosEngine, PhraseLooper) plus the shared Svf and FdnReverb |
| `build_amxd.py` | Builds `Cosmos.amxd`: the patcher, the Live parameters, the Push banks, and the codebox above |
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

REC/PLAY works like the plugin's looper button: REC, then PLAY, then DUB, then back to PLAY.

## Differences from the VST3

- 24 grains instead of 48, to save CPU on Push. Dense HAZE / STRUM settings drop the extra grains.
- 2^20-sample history: about 21 s at 48 kHz, 11 s at 96 kHz. The looper holds 60 s at 48 kHz.
- When SYNC is on, the clock follows Live's tempo and locks to the beat through `plugphasor~` and `plugsync~`.
  There is no TAP button. With SYNC off, use the Tempo knob.
- In POST-FX looper mode, the looper hears the output one sample late.
- The random sequences differ from the plugin's, so renders are not sample-identical.
- The desktop device view shows the looper state. Push only shows the parameters.

## Change it

Edit `cosmos.genexpr` or the layout and parameter tables in `build_amxd.py`, then run:

```sh
python3 plugins/cosmos/m4l/build_amxd.py          # rebuild Cosmos.amxd
python3 plugins/cosmos/m4l/build_amxd.py --check  # fails if Cosmos.amxd is stale
```

Keep the GenExpr in step with `Source/dsp` when the engine changes. Parenthesize any expression that chains 3 or
more operators of mixed precedence, for example `(a > b) && ((c - d) < e)`. GenExpr parsers don't all nest
these the same way.

Never pass a user function's result straight into `poke()` (or `Delay.write()`): assign it to a variable first.
gen~'s type pass never types a user function nested inside `poke()`, so code generation fails with
`gen2.dsp.Model:1455: assertion failed` and the whole device goes silent. That was the cause of the first
"no audio" bug.

Three more gen~ rules, each of which caused silence or a dead feature in Max even though the code looked fine:

- Give every `Param` an explicit `min` and `max`. gen~ defaults a missing range to 0..1 and clamps to it, which
  broke the looper buttons (counters past 1) and Live sync (tempo and beat position).
- Keep integer literals below 2^31. gen~ emits them as C `int`, so `4294967296` became 0.
- Don't pre-declare locals with a constant (`ticked = 0;` at the top, then set inside blocks). gen~'s constant
  folding then treated `if (ticked)` as always false, and no grains ever spawned.

The code also keeps a few conventions: no user function calls another or returns multiple values, and lookup
tables live in the `tbl` Data (filled on the first sample).
