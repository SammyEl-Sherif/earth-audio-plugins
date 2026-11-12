"""Duck for Max for Live: the tempo-synced volume shaper. Built by scripts/build_m4l.py."""

from __future__ import annotations

import re
from pathlib import Path

from aamax import Blob, Choice, Device, Dial, Monitor, ui

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
NODES = 16
CURVE_SIZE = 1 + NODES * 3  # node count, then x y bend per node


def stock_shapes() -> list[list[tuple[float, float, float]]]:
    """The 16 stock curves, read from the C++ source so the device can't drift from the plugin."""
    text = (ROOT / "shared/dsp/include/animalaudio/dsp/StandardPresets.h").read_text(encoding="utf-8")
    rows = re.findall(r"^\s*\{(N\{.*\})\},\s*$", text, re.M)
    node = re.compile(r"N\{(-?[\d.]+)f, (-?[\d.]+)f, (-?[\d.]+)f\}")
    shapes = [[tuple(float(v) for v in m) for m in node.findall(row)] for row in rows]
    assert len(shapes) == 16, f"expected 16 stock shapes, found {len(shapes)}"
    return shapes


def preset_names() -> list[str]:
    text = (ROOT / "plugins/duck/Source/dsp/DuckPresets.cpp").read_text(encoding="utf-8")
    names = re.findall(r'^\s*"([^"]+)",\s*$', text, re.M)
    assert len(names) == 16, f"expected 16 Duck preset names, found {len(names)}"
    return names


SHAPES = stock_shapes()
NAMES = preset_names()


def flatten(nodes) -> list[float]:
    flat = [float(len(nodes))]
    for x, y, b in nodes:
        flat += [x, y, b]
    return flat + [0.0] * (CURVE_SIZE - len(flat))


def gen_inserts() -> dict[str, str]:
    presets = []
    for p, nodes in enumerate(SHAPES):
        for k, v in enumerate(flatten(nodes)[: 1 + 3 * len(nodes)]):
            presets.append(f"poke(pre, {v:g}, {p * CURVE_SIZE + k}, 0);")
    return {
        "curveparams": "\n".join(f"Param c{k}({flatten(SHAPES[0])[k]:g}, min=-1, max=16);" for k in range(CURVE_SIZE)),
        "presets": "\n".join(presets),
        "curvehash": "\n".join(f"hash = hash + (c{k} * {1 + k * 0.618:.3f});" for k in range(CURVE_SIZE)),
        "copycustom": "\n".join(f"poke(cur, c{k}, {k}, 0);" for k in range(CURVE_SIZE)),
    }


PARAMS = [
    Choice("Shape", "preset", items=NAMES + ["Custom"], default=0),
    Choice("Division", "division", items=["1/8", "1/4", "1/2", "1/1"], default=1, short="Rate"),
    Dial("Mix", "drywet", lo=0, hi=100, default=100, unit="%"),
    Blob("Curve", "c", size=CURVE_SIZE, default=flatten(SHAPES[0]), to_gen=True),
]

W = 560
LAYOUT = [
    ui.card((8, 32, 362, 131), "Shape"),
    ui.action("flip", (262, 36, 46, 13), "FLIP"),
    ui.action("invert", (312, 36, 50, 13), "INVERT"),
    ui.widget("duck.curve", (16, 50, 346, 105), param="preset", curve="c"),
    ui.card((376, 32, 176, 131), "Motion"),
    ui.readout((444, 36, 100, 13), "playing", items=["FREE RUN", "SYNCED"], tones=[None, "good"], pill=True,
               align="right", size=7),
    ui.segment("division", (384, 50, 160, 20)),
    ui.stepper("preset", (384, 76, 160, 20)),
    ui.knob("drywet", 384, 100),
    ui.widget("duck.gain", (442, 104, 86, 52)),
    ui.meter((534, 104, 10, 52)),
]

DEVICE = Device(
    name="Duck", slug="duck", kind="audio_effect", folder=HERE, width=W,
    tagline="tempo-synced volume shaper",
    description="DUCK tempo-synced volume shaper by Animal Audio (gen~, Push 3 standalone ready)",
    params=PARAMS,
    banks=[("Duck", ["Shape", "Division", "Mix"])],
    layout=LAYOUT,
    gen="duck.genexpr", transport=True,
    monitors=[Monitor("phase", ms=16), Monitor("gain", ms=16)],
    ui_data={"presets": [list(map(list, s)) for s in SHAPES], "nodes": NODES},
    gen_inserts=gen_inserts(),
)
