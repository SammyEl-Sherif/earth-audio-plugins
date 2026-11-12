# DUCK for Max for Live (Push 3 standalone)

`Duck.amxd` is a Max for Live audio effect port of the Duck VST3: plain Max + gen~, no externals, so it runs on
Push 3 standalone. Built from `device.py`, `duck.genexpr` and `ui.js` by `scripts/build_m4l.py` (see
[shared/m4l/README.md](../../../shared/m4l/README.md)).

## Use it

Drag `Duck.amxd` onto an audio track (or copy it into your User Library). To use it on Push, save a Set that uses it
and send it to Push, or copy it to Push's User Library from Live's browser.

## Push controls

| Bank | 1 | 2 | 3 |
| --- | --- | --- | --- |
| Duck | Shape | Division | Mix |

Shape picks one of the 16 stock curves or **Custom**, the curve drawn on the desktop (saved with the Set, so it plays
on Push too).

## Editing the curve (desktop)

Drag a node, click empty space to add one, double-click to remove, Option-drag a segment to bend it. FLIP and INVERT
mirror the curve. Editing a stock shape copies it into Custom.

## Differences from the VST3

- No Fallback BPM: Live always has a tempo. Stopped, the cycle free-runs at Live's tempo.
- Play/stop and the bar position are polled every 10 ms; the phase within a beat is sample-accurate.
- The stock shapes are read from `shared/dsp/.../StandardPresets.h` at build time, so they can't drift.
