"""Cosmos for Max for Live: the granular looper. Built by scripts/build_m4l.py.

Live parameter names, short names, ranges and gen~ ids match the first (stock live.* controls) device, so Live sets
made with it keep their values. Never rename them.
"""

from __future__ import annotations

from pathlib import Path

from aamax import Button, Choice, Device, Dial, Monitor, Toggle, ui

HERE = Path(__file__).resolve().parent

MODES = ["Mosaic", "Sequence", "Glide", "Haze", "Tunnel", "Strum", "Blocks", "Interrupt", "Arp", "Pattern", "Warp"]
FAMILIES = ["MICRO LOOP", "GRANULES", "GLITCH", "MULTIDELAY"]  # plugins/cosmos/Source/dsp/CosmosModes.h
FAMILY_OF = [0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3]
TIMES = ["1/4", "1/2", "1X", "2X", "4X", "8X"]

PARAMS = [
    Choice("Mode", "mode", items=MODES, default=0),
    Choice("Variation", "variation", items=["A", "B", "C", "D"], default=0),
    Dial("Activity", "activity", lo=0, hi=100, default=50, unit="%"),
    Dial("Shape", "shape", lo=0, hi=100, default=60, unit="%"),
    Dial("Filter", "cutoff", lo=60, hi=20000, default=20000, unit="Hz", centre=1200),
    Dial("Resonance", "resonance", lo=0, hi=100, default=15, unit="%", short="Reso"),
    Dial("Mix", "drywet", lo=0, hi=100, default=50, unit="%"),
    Toggle("Hold", "hold", default=0),
    Choice("Time", "timesel", items=TIMES, default=2),
    Toggle("Sync", "sync", default=1),
    Dial("Tempo", "tempo", lo=40, hi=240, default=120, unit="%.1f BPM"),
    Dial("Repeats", "repeats", lo=0, hi=100, default=30, unit="%"),
    Dial("Mod Freq", "modfreq", lo=0.05, hi=12, default=1, unit="Hz", centre=1.5),
    Dial("Mod Depth", "moddepth", lo=0, hi=100, default=0, unit="%"),
    Dial("Space", "space", lo=0, hi=100, default=30, unit="%"),
    Dial("Reverb Time", "revtime", lo=0.4, hi=20, default=3, unit="%.1f s", centre=3, short="Rev Time"),
    Button("Rec Play Dub", "cmdrec", short="Rec/Play"),
    Button("Loop Stop", "cmdstop", short="Stop"),
    Button("Loop Clear", "cmdclear", short="Clear"),
    Dial("Loop Level", "looplevel", lo=0, hi=100, default=80, unit="%", short="Loop Lvl"),
    Toggle("Loop Reverse", "loopreverse", default=0, short="Loop Rev"),
    Toggle("Loop Pre-FX", "looppre", default=1, short="Pre-FX"),
    Dial("Effect Vol", "effectdb", lo=-24, hi=12, default=0, unit="dB", short="FX Vol"),
]

BANKS = [
    ("Cosmos", ["Mode", "Variation", "Activity", "Shape", "Filter", "Resonance", "Mix", "Hold"]),
    ("Time + FX", ["Time", "Sync", "Tempo", "Repeats", "Mod Freq", "Mod Depth", "Space", "Reverb Time"]),
    ("Looper", ["Rec Play Dub", "Loop Stop", "Loop Clear", "Loop Level", "Loop Reverse", "Loop Pre-FX",
                "Effect Vol", "-"]),
]

W = 700
SMALL = dict(size=30, width=44)  # two knob rows per card
LAYOUT = [
    # Mode: the 11 modes, one family per row, and the variation.
    ui.card((8, 32, 162, 131), "Mode"),
    ui.widget("cosmos.family", (64, 36, 98, 13), param="mode"),
    ui.chips("mode", (16, 47, 146, 76), cols=3, size=8),
    ui.segment("variation", (16, 131, 146, 22)),

    # Grain: what the mode does with the history.
    ui.card((176, 32, 148, 131), "Grain"),
    ui.toggle("hold", (272, 35, 44, 15), label="HOLD", tone="warm"),
    *ui.knob_row(["activity", "shape", "cutoff"], 184, 44, **SMALL),
    ui.knob("resonance", 184, 100, **SMALL),
    ui.knob("modfreq", 228, 100, label="MOD", **SMALL),
    ui.knob("moddepth", 272, 100, label="DEPTH", **SMALL),

    # Echo: clock, feedback and the reverb.
    ui.card((330, 32, 148, 131), "Echo"),
    ui.toggle("sync", (426, 35, 44, 15), label="SYNC", tone="good"),
    ui.segment("timesel", (338, 50, 132, 18), size=8),
    *ui.knob_row(["repeats", "space", "revtime"], 338, 72, **SMALL),
    ui.slider("tempo", (338, 131, 132, 22)),

    # Looper: state, transport, playback options.
    ui.card((484, 32, 144, 131), "Looper"),
    ui.toggle("looppre", (572, 35, 48, 15), label="PRE-FX", led=False),
    ui.widget("cosmos.looper", (492, 48, 128, 46)),
    ui.widget("cosmos.rec", (492, 100, 58, 20), param="cmdrec"),
    ui.button("cmdstop", (553, 100, 32, 20), label="STOP", led=False, size=7),
    ui.button("cmdclear", (588, 100, 32, 20), label="CLEAR", led=False, size=7),
    ui.toggle("loopreverse", (492, 133, 40, 18), label="REV", led=False),
    ui.slider("looplevel", (538, 129, 82, 24), label="LEVEL"),

    # Output.
    ui.card((634, 32, 58, 131), "Out"),
    ui.knob("drywet", 634, 44, **SMALL),
    ui.knob("effectdb", 634, 100, label="FX VOL", **SMALL),
    ui.meter((680, 46, 6, 106)),
]

DEVICE = Device(
    name="Cosmos", slug="cosmos", kind="audio_effect", folder=HERE, width=W,
    tagline="granular looper",
    description="COSMOS granular looper by Animal Audio (gen~ port, Push 3 standalone ready)",
    params=PARAMS,
    banks=BANKS,
    layout=LAYOUT,
    gen="cosmos.genexpr", transport=True,
    monitors=[Monitor("lstate", ms=33), Monitor("lpos", ms=16), Monitor("llen", ms=100)],
    ui_data={"families": FAMILIES, "familyOf": FAMILY_OF},
)
