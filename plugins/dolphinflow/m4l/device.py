"""Dolphin Flow for Max for Live: the chord instrument with glide. Built by scripts/build_m4l.py.

MIDI in (midiin -> midiparse) -> logic.js (held keys, chord, voice leading, the loop's voicings) -> gen~ (glide
synth, envelope, filter, reverb, the loop's timing) -> audio out. See README.md.
"""

from __future__ import annotations

import re
from pathlib import Path

from aamax import Button, Choice, Device, Dial, Monitor, Toggle, ui

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "plugins/dolphinflow/Source/dsp"
SLOTS = 8
VOICES = 6
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def chord_shapes() -> list[tuple[str, str, list[int]]]:
    """(label, symbol, intervals) from Chords.h, so the device can't drift from the plugin."""
    text = (SOURCE / "Chords.h").read_text(encoding="utf-8")
    shapes = [(label, symbol, [int(v) for v in ivs.split(",")])
              for label, symbol, _, ivs in re.findall(r'\{"([^"]*)", "([^"]*)", (\d), \{([\d, ]+)\}\}', text)]
    assert len(shapes) == 10, f"expected 10 chord shapes in Chords.h, found {len(shapes)}"
    return shapes


def slot_lengths() -> tuple[list[float], list[str]]:
    text = (SOURCE / "Progression.h").read_text(encoding="utf-8")
    beats = [float(v) for v in re.search(r"kSlotBeats\[\] = \{([^}]*)\}", text).group(1).split(",")]
    labels = re.findall(r'"(\d[^"]*)"', re.search(r"slotLengthLabel.*?\{(.*?)\};", text, re.S).group(1))
    assert len(beats) == len(labels) == 8, "expected 8 slot lengths in Progression.h"
    return beats, labels


SHAPES = chord_shapes()
BEATS, LENGTH_LABELS = slot_lengths()


def check_logic_tables() -> None:
    """logic.js carries its own copy of the tables (Max's js can't read C++): fail the build if they drift."""
    js = (HERE / "logic.js").read_text(encoding="utf-8")
    for label, symbol, ivs in SHAPES:
        row = f'["{label}", "{symbol}", [{", ".join(map(str, ivs))}]]'
        assert row in js, f"logic.js SHAPES is out of step with Chords.h: missing {row}"
    beats = "[" + ", ".join(f"{b:g}" for b in BEATS) + "]"
    assert f"var SLOT_BEATS = {beats};" in js, "logic.js SLOT_BEATS is out of step with Progression.h"


check_logic_tables()

# First open: Am7 - Fmaj7 - C - G, a bar each (DolphinFlowProcessor's defaultSlot).
DEFAULT_SLOTS = [(9, 4), (5, 3), (0, 0), (7, 0)] + [(-1, 0)] * 4

PARAMS = [
    Choice("Chord", "chord", items=[s[0] for s in SHAPES], default=0, to_gen=False),
    Choice("Mode", "mode", items=["Pad", "Stab", "Keys"], default=0),
    Choice("Voices", "voices", items=["3", "4", "5", "6"], default=1, to_gen=False),
    Choice("Octave", "octave", items=["-2", "-1", "0", "+1", "+2"], default=2, to_gen=False),
    Dial("Glide", "glide", lo=0, hi=2000, default=350, unit="ms", centre=300),
    Dial("Curve", "curve", lo=0, hi=100, default=50, unit="%"),
    Dial("Tone", "tone", lo=0, hi=100, default=55, unit="%"),
    Dial("Width", "width", lo=0, hi=100, default=35, unit="%"),
    Dial("Space", "space", lo=0, hi=100, default=25, unit="%"),
    Dial("Level", "level", lo=0, hi=100, default=70, unit="%"),
    Toggle("Latch", "latch", default=0, to_gen=False),
    Toggle("Loop", "loopon", default=0),
    Button("Write Clip", "clip", short="Clip", to_gen=False),
]
for k in range(SLOTS):
    root, shape = DEFAULT_SLOTS[k]
    n = k + 1
    PARAMS += [
        Choice(f"Slot {n} Root", f"s{n}root", items=["Off"] + NOTE_NAMES, default=root + 1, short=f"{n} Root",
               to_gen=False),
        Choice(f"Slot {n} Chord", f"s{n}chord", items=[s[0] for s in SHAPES], default=shape, short=f"{n} Chord",
               to_gen=False),
        Choice(f"Slot {n} Length", f"s{n}len", items=LENGTH_LABELS, default=3, short=f"{n} Length", to_gen=False),
    ]

BANKS = [
    ("Play", ["Chord", "Mode", "Voices", "Octave", "Glide", "Curve", "Latch", "Loop"]),
    ("Sound", ["Tone", "Width", "Space", "Level", "Write Clip", "-", "-", "-"]),
    ("Roots", [f"Slot {n} Root" for n in range(1, SLOTS + 1)]),
    ("Chords", [f"Slot {n} Chord" for n in range(1, SLOTS + 1)]),
    ("Lengths", [f"Slot {n} Length" for n in range(1, SLOTS + 1)]),
]

MONITORS = [Monitor(f"p{v}", ms=33) for v in range(VOICES)] + [
    Monitor("env", ms=33), Monitor("slot", ms=33), Monitor("prog", ms=33), Monitor("active", ms=33),
]
GEN_OUTPUTS = 2


def outlet_of(name: str) -> int:
    return GEN_OUTPUTS + [m.name for m in MONITORS].index(name)


def gen_inserts() -> dict[str, str]:
    params, copy = [], []
    for k in range(1, SLOTS + 1):
        params.append(f"Param t{k}len(0, min=0, max=16);")
        params += [f"Param t{k}n{j}(0, min=0, max=127);" for j in range(VOICES)]
        copy.append(f"poke(lens, t{k}len, {k - 1}, 0);")
        ends = " + ".join(f"t{i}len" for i in range(1, k + 1))
        copy.append(f"poke(ends, {ends}, {k - 1}, 0);")
        copy += [f"poke(tbl, t{k}n{j}, {k - 1}, {j});" for j in range(VOICES)]
    return {"tableparams": "\n".join(params), "tablecopy": "\n".join(copy)}


# ---------------------------------------------------------------- layout (169 px tall; content y 32..163)

X_FLOW, W_FLOW = 8, 200
X_CHORD, W_CHORD = X_FLOW + W_FLOW + 6, 176
X_GLIDE, W_GLIDE = X_CHORD + W_CHORD + 6, 116
X_SOUND, W_SOUND = X_GLIDE + W_GLIDE + 6, 232
X_ARR, W_ARR = X_SOUND + W_SOUND + 6, 248
W = X_ARR + W_ARR + 8


def chord_row(y: float, label: str, param: str, labels: list[str] | None = None) -> list[dict]:
    x = X_CHORD + 8
    return [ui.label((x, y, 40, 18), label, size=7, color="faint", font="bold"),
            ui.segment(param, (x + 40, y, W_CHORD - 56, 18), labels=labels, size=8.5)]


LAYOUT = [
    ui.card((X_FLOW, 32, W_FLOW, 131), "Flow"),
    ui.widget("dolphinflow.flow", (X_FLOW + 8, 48, W_FLOW - 16, 108)),

    ui.card((X_CHORD, 32, W_CHORD, 131), "Chord"),
    ui.toggle("latch", (X_CHORD + W_CHORD - 62, 36, 54, 13), label="LATCH", tone="warm"),
    ui.chips("chord", (X_CHORD + 8, 50, W_CHORD - 16, 38), cols=5, size=8),
    *chord_row(94, "VOICES", "voices"),
    *chord_row(116, "OCTAVE", "octave"),
    *chord_row(138, "MODE", "mode", ["PAD", "STAB", "KEYS"]),

    ui.card((X_GLIDE, 32, W_GLIDE, 131), "Glide"),
    *ui.knob_row(["glide", "curve"], X_GLIDE + 6, 48),
    ui.widget("dolphinflow.glide", (X_GLIDE + 8, 116, W_GLIDE - 16, 40), params=["glide", "curve"]),

    ui.card((X_SOUND, 32, W_SOUND, 131), "Sound"),
    *ui.knob_row(["tone", "width", "space", "level"], X_SOUND + 4, 48),
    ui.meter((X_SOUND + W_SOUND - 18, 52, 10, 56)),
    ui.widget("dolphinflow.env", (X_SOUND + 8, 116, W_SOUND - 16, 40), param="mode"),

    ui.card((X_ARR, 32, W_ARR, 131), "Arrange"),
    ui.widget("dolphinflow.status", (X_ARR + 68, 36, W_ARR - 186, 13)),
    ui.button("clip", (X_ARR + W_ARR - 112, 36, 46, 13), label="CLIP", led=False),
    ui.toggle("loopon", (X_ARR + W_ARR - 62, 36, 54, 13), label="LOOP"),
    ui.widget("dolphinflow.slots", (X_ARR + 8, 50, W_ARR - 16, 106),
              params=[f"s{n}{f}" for n in range(1, SLOTS + 1) for f in ("root", "chord", "len")]),
]


# ---------------------------------------------------------------- MIDI in and the js helper

def extra(ctx) -> None:
    p = ctx.patch
    logic = p.obj(f"js {ctx.file('logic.js')}", 1, 2, column=8)

    midi = p.obj("midiin", 1, 1, ["int"], column=8)
    parse = p.obj("midiparse", 1, 8, column=8)
    p.connect(midi, 0, parse, 0)
    for outlet, name in ((0, "note"), (2, "cc")):
        pre = p.obj(f"prepend {name}", 1, 1, column=8)
        p.connect(parse, outlet, pre, 0)
        p.connect(pre, 0, logic, 0)

    # Parameters the chord logic reads (their stored values are re-sent at load, like every parameter).
    for param in PARAMS:
        if param.id == "clip" or (param.to_gen and param.id != "loopon"):
            continue
        pre = p.obj(f"prepend {param.id}", 1, 1, column=9)
        p.connect(ctx.params[param.id], 0, pre, 0)
        p.connect(pre, 0, logic, 0)

    # Write Clip: the button's press counter (one change per press).
    pre = p.obj("prepend clip", 1, 1, column=9)
    p.connect(ctx.sources["clip"], 0, pre, 0)
    p.connect(pre, 0, logic, 0)

    # gen~'s state back to the logic: is anything sounding (glide or start fresh), and the loop's slot.
    for name, selector in (("active", "active"), ("slot", "seqslot")):
        probe = p.obj("snapshot~ 10", 2, 1, ["float"], column=9)
        p.connect(ctx.gen, outlet_of(name), probe, 0)
        change = p.obj("change", 1, 3, ["", "int", "int"], column=9)
        p.connect(probe, 0, change, 0)
        pre = p.obj(f"prepend {selector}", 1, 1, column=9)
        p.connect(change, 0, pre, 0)
        p.connect(pre, 0, logic, 0)

    ready = p.message("ready", column=9)
    p.connect(ctx.loadbang, 0, ready, 0)
    p.connect(ready, 0, logic, 0)

    p.connect(logic, 0, ctx.gen, 0)
    mon = p.obj("prepend mon", 1, 1, column=9)
    p.connect(logic, 1, mon, 0)
    p.connect(mon, 0, ctx.ui, 0)
    p.connect(ctx.ui_router, ctx.ext_outlet, logic, 0)


DEVICE = Device(
    name="Dolphin Flow", slug="dolphinflow", kind="instrument", folder=HERE, width=W,
    tagline="chord glider",
    description="DOLPHIN FLOW chord instrument with glide by Animal Audio (gen~ + js, Push 3 standalone ready)",
    params=PARAMS,
    banks=BANKS,
    layout=LAYOUT,
    gen="dolphinflow.genexpr", gen_inputs=0, gen_outputs=GEN_OUTPUTS, transport=True,
    monitors=MONITORS,
    ui_data={"shapes": [[s[0], s[1]] for s in SHAPES], "names": NOTE_NAMES, "lengths": LENGTH_LABELS,
             "beats": BEATS, "slots": SLOTS},
    gen_inserts=gen_inserts(),
    files=["logic.js"],
    extra=extra,
)
