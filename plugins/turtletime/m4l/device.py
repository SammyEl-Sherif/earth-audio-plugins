"""Turtle Time for Max for Live: the tempo-locked time warp. Built by scripts/build_m4l.py."""

from __future__ import annotations

import re
from pathlib import Path

from aamax import Choice, Device, Dial, Monitor, Toggle, ui

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
PATTERNS_H = ROOT / "plugins/turtletime/Source/dsp/TimePatterns.h"


def read_patterns() -> dict:
    """Pattern table and labels, read from the C++ source so the device can't drift from the plugin."""
    text = PATTERNS_H.read_text(encoding="utf-8")
    rows = re.findall(r'\{"([^"]+)",\s*"([^"]*)",\s*(-?[\d.]+)f,\s*([\d.]+),\s*([\d.]+),\s*TimeMode::(\w+)\}', text)
    assert len(rows) == 32, f"expected 32 Turtle Time patterns, found {len(rows)}"

    def strings(name: str) -> list[str]:
        body = re.search(name + r"\{([^}]*)\}", text).group(1)
        return re.findall(r'"([^"]+)"', body)

    default = int(re.search(r"kDefaultPattern\s*=\s*(\d+)", text).group(1))
    return {
        "names": [r[0] for r in rows],
        "blurbs": [r[1] for r in rows],
        # rate, window beats, slice beats, mode (0 catch-up, 1 repeat)
        "table": [[float(r[2]), float(r[3]), float(r[4]), 1.0 if r[5] == "Repeat" else 0.0] for r in rows],
        "groups": strings("kGroupNames"),
        "details": strings("kGroupDetails"),
        "lengths": strings("kLengthNames"),
        "default": default,
    }


PATTERNS = read_patterns()


def gen_inserts() -> dict[str, str]:
    lines = []
    for i, row in enumerate(PATTERNS["table"]):
        for ch, v in enumerate(row):
            lines.append(f"poke(pat, {v:g}, {i}, {ch});")
    return {"patterns": "\n".join(lines)}


PARAMS = [
    Choice("Pattern", "pattern", items=PATTERNS["names"], default=PATTERNS["default"]),
    Dial("Mix", "drywet", lo=0, hi=100, default=100, unit="%"),
    Toggle("Keep Pitch", "keeppitch", default=0),
]

W = 656
LAYOUT = [
    ui.card((8, 32, 200, 131), "Window"),
    ui.readout((120, 36, 80, 13), "playing", items=["STOPPED", "SYNCED"], tones=[None, "good"], pill=True,
               align="right", size=7),
    ui.widget("turtletime.scope", (16, 50, 184, 105), param="pattern"),
    ui.card((214, 32, 316, 131), "Pattern"),
    ui.widget("turtletime.matrix", (222, 36, 300, 119), param="pattern"),
    ui.card((536, 32, 112, 131), "Output"),
    ui.readout((576, 36, 64, 13), "bpm", fmt="%.0f BPM", pill=True, align="right", size=7),
    ui.knob("drywet", 544, 50),
    ui.meter((616, 52, 10, 56)),
    ui.widget("turtletime.pitch", (544, 129, 96, 20), param="keeppitch", labels=["DROP", "KEEP"]),
]

DEVICE = Device(
    name="Turtle Time", slug="turtletime", kind="audio_effect", folder=HERE, width=W,
    tagline="tempo-locked time warp",
    description="TURTLE TIME tempo-locked half-time, reverse and repeat by Animal Audio (gen~, Push 3 standalone ready)",
    params=PARAMS,
    banks=[("Turtle Time", ["Pattern", "Mix", "Keep Pitch"])],
    layout=LAYOUT,
    gen="turtletime.genexpr", transport=True,
    monitors=[
        Monitor("phase", ms=16),
        Monitor("marker", ms=16),
        Monitor("inpk", mode="peak", ms=33),
        Monitor("outpk", mode="peak", ms=33),
        Monitor("bpm", ms=250),
    ],
    ui_data={k: PATTERNS[k] for k in ("blurbs", "table", "groups", "details", "lengths")},
    gen_inserts=gen_inserts(),
)
