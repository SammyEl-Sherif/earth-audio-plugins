"""Parrot for Max for Live: the pitch / formant voice bender. Built by scripts/build_m4l.py."""

from __future__ import annotations

import math
from pathlib import Path

from aamax import Choice, Device, Dial, Monitor, Toggle, ui

HERE = Path(__file__).resolve().parent

# ---------------------------------------------------------------- latency (mirrors VoiceShifter::prepare)
#
# Live's "Defined Latency" is a fixed number of samples, so the device uses one budget at every rate: the VST3's
# formula at 44.1 and 48 kHz (cubic reads need 3 samples of reach instead of the sinc's 13), plus a little slack.
# parrot.genexpr gets the same number and, above 48 kHz, raises the lowest tracked pitch so its design still fits.

READ_REACH = 3
DRIVE_LATENCY = 23  # 2x halfband oversampler: (47 - 1) / 2 os samples each way


def tracker(sr: float, lag_cap: int | None = None) -> dict:
    d = max(1, round(sr / 12000.0))
    coarse = sr / d
    hop = max(1, round(coarse * 0.0027))
    max_lag = math.ceil(coarse / 75.0)
    if lag_cap is not None:
        max_lag = max(16, min(max_lag, lag_cap))
    span = 2 * max_lag + 2
    max_period = max_lag * d
    centre_lag = (span * d) // 2 + d
    mark_lag = max(centre_lag + hop * d - max_period // 2, max_period // 2 + 4)
    need = mark_lag + math.ceil(max_period * 4.375) + READ_REACH + 8
    return {"d": d, "hop": hop, "max_lag": max_lag, "need": need, "min_hz": coarse / max_lag}


SHIFTER_LATENCY = max(tracker(sr)["need"] for sr in (44100.0, 48000.0)) + 16
LATENCY = SHIFTER_LATENCY + DRIVE_LATENCY


def gen_lag_cap(sr: float) -> int:
    """The cap parrot.genexpr applies (keep the two in step)."""
    t = tracker(sr)
    pcap = math.floor((SHIFTER_LATENCY - 2 * t["d"] - t["hop"] * t["d"] - 18) / 4.875)
    return math.floor(pcap / t["d"])


for _sr in (44100.0, 48000.0):
    assert gen_lag_cap(_sr) >= tracker(_sr)["max_lag"], "the latency budget must not limit 44.1 / 48 kHz"
for _sr in (22050.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0):
    assert tracker(_sr, gen_lag_cap(_sr))["need"] <= SHIFTER_LATENCY, f"latency budget too small at {_sr}"


def halfband(taps: int = 47, beta: float = 9.0) -> list[float]:
    """dsp::designLowpass(taps, 0.25, beta): the 2x oversampler's halfband (unity DC gain)."""

    def i0(x: float) -> float:
        total, term, q = 1.0, 1.0, 0.25 * x * x
        for k in range(1, 200):
            term *= q / (k * k)
            total += term
            if term < total * 1e-15:
                break
        return total

    centre = (taps - 1) / 2
    out = []
    for n in range(taps):
        t = n - centre
        r = 1.0 - (t / centre) ** 2
        window = i0(beta * math.sqrt(max(r, 0.0))) / i0(beta)
        x = 0.5 * t
        sinc = 1.0 if abs(x) < 1e-12 else math.sin(math.pi * x) / (math.pi * x)
        out.append(0.5 * sinc * window)
    total = sum(out)
    out = [v / total for v in out]
    for n in range(1, taps, 2):
        if n != int(centre):
            assert abs(out[n]) < 1e-12
    return out


# ---------------------------------------------------------------- parameters (names = the VST3's, ParrotProcessor.cpp)

NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
LOWEST_NOTE = 36  # C2 (C4 = MIDI 60)
NOTES = [f"{NAMES[m % 12]}{m // 12 - 1}" for m in range(LOWEST_NOTE, LOWEST_NOTE + 48)]

PARAMS = [
    Dial("Pitch", "pitch", lo=-12, hi=12, default=0, unit="st", bipolar=True),
    Dial("Formant", "formant", lo=-12, hi=12, default=0, unit="st", bipolar=True),
    Toggle("Fine", "fine", default=0),
    Toggle("Link", "link", default=0),
    Choice("Mode", "mode", items=["Shift", "Snap", "Robot"], default=0),
    Choice("Robot Note", "note", items=NOTES, default=48 - LOWEST_NOTE, short="Note"),
    Dial("Glide", "glide", lo=0, hi=500, default=0, unit="ms", centre=60),
    Dial("Drive", "drive", lo=0, hi=100, default=0, unit="%"),
    Dial("Mix", "drywet", lo=0, hi=100, default=100, unit="%"),
]

# ---------------------------------------------------------------- layout

W = 760
LAYOUT = [
    ui.card((8, 32, 124, 131), "Voice"),
    ui.knob("pitch", 14, 54, size=44, width=56),
    ui.knob("formant", 70, 54, size=44, width=56),
    ui.toggle("fine", (16, 137, 52, 18), label="FINE"),
    ui.toggle("link", (72, 137, 52, 18), label="LINK"),

    ui.card((138, 32, 288, 131), "Pitch trace"),
    ui.widget("parrot.status", (230, 36, 188, 13)),
    ui.widget("parrot.trace", (146, 50, 272, 107)),

    ui.card((432, 32, 168, 131), "Mode"),
    ui.segment("mode", (440, 50, 152, 20)),
    ui.stepper("note", (440, 76, 152, 20)),
    ui.knob("glide", 440, 100),
    ui.widget("parrot.target", (500, 104, 92, 52)),

    ui.card((606, 32, 146, 131), "Output"),
    ui.knob("drive", 612, 54, size=44, width=56),
    ui.knob("drywet", 668, 54, size=44, width=56),
    ui.widget("parrot.tube", (616, 135, 104, 20)),
    ui.meter((732, 50, 10, 105)),
]


# ---------------------------------------------------------------- MIDI: held notes -> gen~ `held` (last note priority)


def extra(ctx) -> None:
    """notein -> a stack of held notes (zl list, -1 sentinel at the bottom) -> `held <newest or -1>` to gen~ and UI.

    Note on: drop the note from the stack, then append it. Note off: drop it. The newest held note is the last
    element; with none held only the sentinel is left. Plain Max objects only, so it runs on Push.
    """
    p = ctx.patch
    col = 8
    notein = p.obj("notein", 1, 3, ["int", "int", "int"], column=col)
    ison = p.obj("> 0", 2, 1, ["int"], column=col)
    pick = p.obj("+ 1", 2, 1, ["int"], column=col)  # 1 = note off, 2 = note on
    gate = p.obj("gate 2", 2, 2, ["", ""], column=col)
    order = p.obj("t b i i", 1, 3, ["bang", "int", "int"], column=col)
    reg = p.obj("zl.reg -1", 2, 2, ["", ""], column=col)
    drop = p.obj("zl.filter", 2, 2, ["", ""], column=col)
    push = p.obj("zl.join", 2, 2, ["", ""], column=col)
    store = p.obj("t l l", 1, 2, ["", ""], column=col)
    newest = p.obj("zl.ecils 1", 2, 2, ["", ""], column=col)
    to_gen = p.obj("prepend held", 1, 1, column=col)
    reset = p.message("-1", column=col)
    panic = p.obj("ctlin 123", 1, 2, ["int", "int"], column=col)

    p.connect(notein, 1, ison, 0)  # velocity arrives before pitch (right to left)
    p.connect(ison, 0, pick, 0)
    p.connect(pick, 0, gate, 0)
    p.connect(notein, 0, order, 0)
    p.connect(order, 2, drop, 1)  # the note to remove
    p.connect(order, 1, push, 1)  # the note to append (note on)
    p.connect(order, 0, reg, 0)  # the stack -> zl.filter
    p.connect(reg, 0, drop, 0)
    p.connect(drop, 0, gate, 1)
    p.connect(gate, 0, store, 0)  # note off: the stack without it
    p.connect(gate, 1, push, 0)  # note on: ... then it on top
    p.connect(push, 0, store, 0)
    p.connect(store, 1, reg, 1)
    p.connect(store, 0, newest, 0)
    p.connect(newest, 1, to_gen, 0)
    p.connect(to_gen, 0, ctx.gen, 0)
    ctx.to_ui(newest, 1, "held")

    p.connect(ctx.loadbang, 0, reset, 0)  # device load and all-notes-off clear the stack
    p.connect(panic, 0, reset, 0)
    p.connect(reset, 0, store, 0)


FIR = halfband()

DEVICE = Device(
    name="Parrot", slug="parrot", kind="audio_effect", folder=HERE, width=W,
    tagline="pitch + formant voice bender",
    description="PARROT pitch and formant voice bender by Animal Audio (gen~, Push 3 standalone ready). "
                "ROBOT follows MIDI notes routed to the track.",
    params=PARAMS,
    banks=[
        ("Parrot", ["Pitch", "Formant", "Mode", "Robot Note", "Glide", "Drive", "Mix", "Link"]),
        ("Options", ["Fine", "Link", "Mode", "Robot Note", "Glide", "-", "-", "-"]),
    ],
    layout=LAYOUT,
    gen="parrot.genexpr", latency=LATENCY,
    monitors=[Monitor("inhz", ms=16), Monitor("outhz", ms=16), Monitor("heat", ms=33)],
    ui_data={"latencySamples": LATENCY, "lowest": LOWEST_NOTE},
    gen_inserts={
        "latency": f"LAT = {SHIFTER_LATENCY};\nDLAT = {DRIVE_LATENCY};",
        "fir": "\n".join(f"poke(fir, {v:.9f}, {k}, 0);" for k, v in enumerate(FIR)),
    },
    extra=extra,
)
