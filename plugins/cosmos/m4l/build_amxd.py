#!/usr/bin/env python3
"""Build Cosmos.amxd (Max for Live audio effect) from cosmos.genexpr.

The device is plain Max + gen~ (no externals, no JavaScript), so it runs on Push 3 standalone. Every control is a
Live parameter, grouped into three 8-knob Push banks. Run after editing cosmos.genexpr or the layout below:

    python3 plugins/cosmos/m4l/build_amxd.py            # writes plugins/cosmos/m4l/Cosmos.amxd
    python3 plugins/cosmos/m4l/build_amxd.py --check    # exit 1 if Cosmos.amxd is stale
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
GENEXPR = HERE / "cosmos.genexpr"
OUTPUT = HERE / "Cosmos.amxd"

MODES = ["Mosaic", "Sequence", "Glide", "Haze", "Tunnel", "Strum", "Blocks", "Interrupt", "Arp", "Pattern", "Warp"]
TIMES = ["1/4", "1/2", "1X", "2X", "4X", "8X"]

# Cosmos palette (plugins/cosmos/Source/ui/CosmosTheme.cpp).
CYAN = [0.322, 0.898, 0.949, 1.0]
GOLD = [1.0, 0.824, 0.353, 1.0]
PINK = [1.0, 0.373, 0.635, 1.0]
CREAM = [0.953, 0.925, 0.863, 1.0]
DIM = [0.557, 0.573, 0.682, 1.0]
PANEL = [0.055, 0.067, 0.125, 1.0]
RAISED = [0.098, 0.114, 0.2, 1.0]

DEVICE_WIDTH = 600

# UNIT styles of live.dial: 3 Hz, 4 dB, 5 %, 9 custom (parameter_units is a printf format).
UNIT_HZ, UNIT_DB, UNIT_PERCENT, UNIT_CUSTOM = 3, 4, 5, 9


def skew_exponent(low: float, high: float, centre: float) -> float:
    """live.dial maps x in 0..1 to low + (high-low) * x^e; pick e so the knob's middle lands on `centre`."""
    import math

    return math.log((centre - low) / (high - low)) / math.log(0.5)


# kind, longname, shortname, gen param, extra. Order inside each bank is the Push encoder order.
DIALS = {
    "Activity": dict(gen="activity", lo=0, hi=100, default=50, unit=UNIT_PERCENT),
    "Shape": dict(gen="shape", lo=0, hi=100, default=60, unit=UNIT_PERCENT),
    "Filter": dict(gen="cutoff", lo=60, hi=20000, default=20000, unit=UNIT_HZ, exp=skew_exponent(60, 20000, 1200)),
    "Resonance": dict(gen="resonance", lo=0, hi=100, default=15, unit=UNIT_PERCENT, short="Reso"),
    "Mix": dict(gen="drywet", lo=0, hi=100, default=50, unit=UNIT_PERCENT),
    "Repeats": dict(gen="repeats", lo=0, hi=100, default=30, unit=UNIT_PERCENT),
    "Space": dict(gen="space", lo=0, hi=100, default=30, unit=UNIT_PERCENT),
    "Reverb Time": dict(gen="revtime", lo=0.4, hi=20, default=3, unit=UNIT_CUSTOM, units="%.1f s",
                        exp=skew_exponent(0.4, 20, 3), short="Rev Time"),
    "Mod Freq": dict(gen="modfreq", lo=0.05, hi=12, default=1, unit=UNIT_HZ, exp=skew_exponent(0.05, 12, 1.5)),
    "Mod Depth": dict(gen="moddepth", lo=0, hi=100, default=0, unit=UNIT_PERCENT),
    "Tempo": dict(gen="tempo", lo=40, hi=240, default=120, unit=UNIT_CUSTOM, units="%.1f BPM"),
    "Effect Vol": dict(gen="effectdb", lo=-24, hi=12, default=0, unit=UNIT_DB, short="FX Vol"),
    "Loop Level": dict(gen="looplevel", lo=0, hi=100, default=80, unit=UNIT_PERCENT, short="Loop Lvl"),
}
CHOICES = {
    "Mode": dict(gen="mode", items=MODES, default=0, cls="live.menu"),
    "Variation": dict(gen="variation", items=["A", "B", "C", "D"], default=0, cls="live.tab"),
    "Time": dict(gen="timesel", items=TIMES, default=2, cls="live.tab"),
}
TOGGLES = {
    "Hold": dict(gen="hold", default=0, text="HOLD"),
    "Sync": dict(gen="sync", default=1, text="SYNC"),
    "Loop Reverse": dict(gen="loopreverse", default=0, text="REV", short="Loop Rev"),
    "Loop Pre-FX": dict(gen="looppre", default=1, text="PRE-FX", short="Pre-FX"),
}
BUTTONS = {
    "Rec Play Dub": dict(gen="cmdrec", text="REC/PLAY", short="Rec/Play"),
    "Loop Stop": dict(gen="cmdstop", text="STOP", short="Stop"),
    "Loop Clear": dict(gen="cmdclear", text="CLEAR", short="Clear"),
}
BANKS = [
    ("Cosmos", ["Mode", "Variation", "Activity", "Shape", "Filter", "Resonance", "Mix", "Hold"]),
    ("Time + FX", ["Time", "Sync", "Tempo", "Repeats", "Mod Freq", "Mod Depth", "Space", "Reverb Time"]),
    ("Looper", ["Rec Play Dub", "Loop Stop", "Loop Clear", "Loop Level", "Loop Reverse", "Loop Pre-FX",
                "Effect Vol", "-"]),
]

# Presentation layout (x, y, w, h) for each control; the device strip is 169 px tall.
LAYOUT = {
    "Mode": (84, 6, 96, 18),
    "Variation": (186, 6, 96, 18),
    "Time": (300, 6, 168, 18),
    "Sync": (474, 6, 44, 18),
    "Activity": (8, 30, 52, 50),
    "Shape": (64, 30, 52, 50),
    "Filter": (120, 30, 52, 50),
    "Resonance": (176, 30, 52, 50),
    "Mix": (232, 30, 52, 50),
    "Repeats": (300, 30, 52, 50),
    "Space": (356, 30, 52, 50),
    "Reverb Time": (412, 30, 52, 50),
    "Mod Freq": (8, 84, 52, 50),
    "Mod Depth": (64, 84, 52, 50),
    "Tempo": (120, 84, 52, 50),
    "Effect Vol": (176, 84, 52, 50),
    "Loop Level": (300, 84, 52, 50),
    "Hold": (232, 88, 52, 40),
    "Rec Play Dub": (356, 88, 108, 20),
    "Loop Stop": (356, 112, 52, 18),
    "Loop Clear": (412, 112, 52, 18),
    "Loop Reverse": (474, 88, 44, 18),
    "Loop Pre-FX": (474, 112, 44, 18),
}


class Patch:
    def __init__(self) -> None:
        self.boxes: list[dict] = []
        self.lines: list[dict] = []
        self.parameters: dict[str, list] = {}
        self.next_id = 1
        self.row_y: dict[int, float] = {}

    def add(self, box: dict, column: int = 0) -> str:
        box_id = f"obj-{self.next_id}"
        self.next_id += 1
        y = self.row_y.get(column, 20.0)
        self.row_y[column] = y + 40.0
        box.setdefault("patching_rect", [20.0 + column * 170.0, y, 150.0, 22.0])
        box["id"] = box_id
        self.boxes.append({"box": box})
        return box_id

    def obj(self, text: str, inlets: int, outlets: int, outlettype: list[str], column: int = 0, **extra) -> str:
        return self.add(dict(maxclass="newobj", text=text, numinlets=inlets, numoutlets=outlets,
                             outlettype=outlettype, **extra), column)

    def connect(self, src: str, outlet: int, dst: str, inlet: int) -> None:
        self.lines.append({"patchline": {"destination": [dst, inlet], "source": [src, outlet]}})

    def param(self, box_id: str, longname: str, shortname: str) -> None:
        self.parameters[box_id] = [longname, shortname, 0]


def presentation(name: str) -> dict:
    x, y, w, h = LAYOUT[name]
    return {"presentation": 1, "presentation_rect": [float(x), float(y), float(w), float(h)]}


def comment(patch: Patch, text: str, rect: tuple, colour: list, size: float = 10.0, bold: bool = False) -> str:
    return patch.add(dict(maxclass="comment", text=text, numinlets=1, numoutlets=0,
                          textcolor=colour, fontsize=size, fontface=1 if bold else 0, presentation=1,
                          presentation_rect=[float(v) for v in rect]), column=5)


def build_patch(code: str) -> dict:
    p = Patch()

    # Audio: plugin~ -> gen~ (with Live's beat ramp on the third inlet) -> plugout~.
    plugin = p.obj("plugin~", 2, 2, ["signal", "signal"], column=0)
    phasor = p.obj("plugphasor~", 1, 1, ["signal"], column=1)
    gen = p.add(dict(maxclass="newobj", text="gen~", numinlets=3, numoutlets=4,
                     outlettype=["signal", "signal", "signal", "signal"], patcher=gen_patcher(code),
                     patching_rect=[20.0, 420.0, 320.0, 22.0]))
    plugout = p.obj("plugout~", 2, 2, ["signal", "signal"], column=0, patching_rect=[20.0, 480.0, 150.0, 22.0])
    p.connect(plugin, 0, gen, 0)
    p.connect(plugin, 1, gen, 1)
    p.connect(phasor, 0, gen, 2)
    p.connect(gen, 0, plugout, 0)
    p.connect(gen, 1, plugout, 1)

    # Host transport -> gen~ params (polled every 10 ms).
    device = p.obj("live.thisdevice", 1, 3, ["bang", "int", "int"], column=2)
    metro = p.obj("metro 10", 2, 1, ["bang"], column=2)
    sync = p.obj("plugsync~", 1, 9, ["int", "int", "int", "float", "list", "float", "float", "int", "int"], column=2)
    p.connect(device, 0, metro, 0)
    p.connect(metro, 0, sync, 0)
    for outlet, name in ((0, "hostplaying"), (5, "hostspb"), (6, "hostbeats")):
        pre = p.obj(f"prepend {name}", 1, 1, [""], column=2)
        p.connect(sync, outlet, pre, 0)
        p.connect(pre, 0, gen, 0)

    ids: dict[str, str] = {}

    for name, spec in DIALS.items():
        short = spec.get("short", name)
        valueof = {
            "parameter_longname": name, "parameter_shortname": short, "parameter_type": 0,
            "parameter_mmin": spec["lo"], "parameter_mmax": spec["hi"], "parameter_initial": [spec["default"]],
            "parameter_initial_enable": 1, "parameter_unitstyle": spec["unit"],
        }
        if "exp" in spec:
            valueof["parameter_exponent"] = round(spec["exp"], 4)
        if "units" in spec:
            valueof["parameter_units"] = spec["units"]
        box = dict(maxclass="live.dial", numinlets=1, numoutlets=2, outlettype=["", "float"], parameter_enable=1,
                   varname=name, activedialcolor=CYAN, saved_attribute_attributes={"valueof": valueof},
                   **presentation(name))
        ids[name] = p.add(box, column=3)

    for name, spec in CHOICES.items():
        valueof = {
            "parameter_longname": name, "parameter_shortname": name, "parameter_type": 2,
            "parameter_enum": spec["items"], "parameter_mmax": len(spec["items"]) - 1,
            "parameter_initial": [spec["default"]], "parameter_initial_enable": 1, "parameter_unitstyle": 9,
        }
        box = dict(maxclass=spec["cls"], numinlets=1, numoutlets=3, outlettype=["", "", "float"],
                   parameter_enable=1, varname=name, saved_attribute_attributes={"valueof": valueof},
                   **presentation(name))
        if spec["cls"] == "live.tab":
            box.update(num_lines_presentation=1, num_lines_patching=1, multiline=0, activebgoncolor=CYAN)
        ids[name] = p.add(box, column=3)

    for name, spec in TOGGLES.items():
        valueof = {
            "parameter_longname": name, "parameter_shortname": spec.get("short", name), "parameter_type": 2,
            "parameter_enum": ["off", "on"], "parameter_mmax": 1, "parameter_initial": [spec["default"]],
            "parameter_initial_enable": 1,
        }
        box = dict(maxclass="live.text", numinlets=1, numoutlets=2, outlettype=["", ""], parameter_enable=1,
                   varname=name, text=spec["text"], texton=spec["text"], mode=1,
                   bgoncolor=GOLD if name == "Hold" else CYAN, saved_attribute_attributes={"valueof": valueof},
                   **presentation(name))
        ids[name] = p.add(box, column=3)

    for name, spec in BUTTONS.items():
        valueof = {
            "parameter_longname": name, "parameter_shortname": spec.get("short", name), "parameter_type": 2,
            "parameter_enum": ["off", "on"], "parameter_mmax": 1,
        }
        box = dict(maxclass="live.text", numinlets=1, numoutlets=2, outlettype=["", ""], parameter_enable=1,
                   varname=name, text=spec["text"], texton=spec["text"], mode=0,
                   activebgoncolor=PINK if name == "Rec Play Dub" else CYAN,
                   saved_attribute_attributes={"valueof": valueof}, **presentation(name))
        ids[name] = p.add(box, column=3)

    for name, box_id in ids.items():
        box = next(b["box"] for b in p.boxes if b["box"]["id"] == box_id)
        p.param(box_id, name, box["saved_attribute_attributes"]["valueof"]["parameter_shortname"])
        if name in BUTTONS:
            # A press bangs; a counter turns it into a new value, which gen~ sees as a change.
            counter = p.obj("counter 1 1000000", 5, 4, ["int", "", "", "int"], column=4)
            p.connect(box_id, 0, counter, 0)
            source = counter
            gen_name = BUTTONS[name]["gen"]
        else:
            source = box_id
            gen_name = (DIALS.get(name) or CHOICES.get(name) or TOGGLES[name])["gen"]
            if name not in TOGGLES:
                p.connect(device, 0, box_id, 0)  # re-send dial / menu values once the device is ready
        pre = p.obj(f"prepend {gen_name}", 1, 1, [""], column=4)
        p.connect(source, 0, pre, 0)
        p.connect(pre, 0, gen, 0)

    # Labels, looper state, meters.
    comment(p, "COSMOS", (8, 4, 74, 22), GOLD, size=15.0, bold=True)
    state_label = comment(p, "LOOP EMPTY", (474, 134, 120, 18), DIM)
    snap = p.obj("snapshot~ 50", 2, 1, ["float"], column=5)
    change = p.obj("change", 1, 3, ["", "int", "int"], column=5)
    select = p.obj("sel 0 1 2 3 4", 6, 6, ["bang", "bang", "bang", "bang", "bang", ""], column=5)
    p.connect(gen, 2, snap, 0)
    p.connect(snap, 0, change, 0)
    p.connect(change, 0, select, 0)
    for index, text in enumerate(["LOOP EMPTY", "RECORDING", "PLAYING", "OVERDUB", "LOOP STOPPED"]):
        msg = p.add(dict(maxclass="message", text=f"set {text}", numinlets=2, numoutlets=1, outlettype=[""]),
                    column=6)
        p.connect(select, index, msg, 0)
        p.connect(msg, 0, state_label, 0)
    for channel, x in ((0, 578), (1, 586)):
        meter = p.add(dict(maxclass="live.meter~", numinlets=1, numoutlets=2, outlettype=["float", "int"],
                           presentation=1, presentation_rect=[float(x), 30.0, 6.0, 100.0]), column=6)
        p.connect(gen, channel, meter, 0)
    comment(p, "Animal Audio", (8, 140, 120, 18), DIM)

    parameters = dict(sorted(p.parameters.items(), key=lambda kv: int(kv[0].split("-")[1])))
    by_name = {v[0]: k for k, v in p.parameters.items()}
    for _, names in BANKS:
        for n in names:
            assert n == "-" or n in by_name, n
    parameters["parameterbanks"] = {
        str(i): {"index": i, "name": bank, "parameters": names} for i, (bank, names) in enumerate(BANKS)
    }
    parameters["inherited_shortname"] = 1

    return {
        "patcher": {
            "fileversion": 1,
            "appversion": {"major": 8, "minor": 6, "revision": 0, "architecture": "x64", "modernui": 1},
            "classnamespace": "box",
            "rect": [100.0, 100.0, 1240.0, 760.0],
            "openrect": [0.0, 0.0, float(DEVICE_WIDTH), 169.0],
            "openinpresentation": 1,
            "default_fontsize": 10.0,
            "default_fontface": 0,
            "default_fontname": "Arial Bold",
            "gridsize": [8.0, 8.0],
            "statusbarvisible": 0,
            "devicewidth": float(DEVICE_WIDTH),
            "description": "COSMOS granular looper by Animal Audio (gen~ port, Push 3 standalone ready)",
            "boxes": p.boxes,
            "lines": p.lines,
            "parameters": parameters,
            "dependency_cache": [],
            "latency": 0,
            "is_mpe": 0,
            "minimum_live_version": "",
            "minimum_max_version": "",
            "platform_compatibility": 0,
            "project": {
                "version": 1, "creationdate": 3590052786, "modificationdate": 3590052786,
                "viewrect": [0.0, 0.0, 300.0, 500.0], "autoorganize": 1, "hideprojectwindow": 1,
                "showdependencies": 1, "autolocalize": 0, "contents": {"patchers": {}}, "layout": {},
                "searchpath": {}, "detailsvisible": 0, "amxdtype": 1633771873, "readonly": 0, "devpathtype": 0,
                "devpath": ".", "sortmode": 0, "viewmode": 0, "includepackages": 0,
            },
            "autosave": 0,
            "bgcolor": PANEL,
            "editing_bgcolor": RAISED,
        }
    }


def gen_patcher(code: str) -> dict:
    boxes, lines = [], []

    def box(box_id: str, **fields) -> None:
        boxes.append({"box": {"id": box_id, **fields}})

    box("gen-code", maxclass="codebox", code=code, fontface=0, fontname="<Monospaced>", fontsize=12.0,
        numinlets=3, numoutlets=4, outlettype=["", "", "", ""], patching_rect=[20.0, 60.0, 760.0, 600.0])
    for i in range(3):
        box(f"gen-in{i + 1}", maxclass="newobj", text=f"in {i + 1}", numinlets=0, numoutlets=1, outlettype=[""],
            patching_rect=[20.0 + 120.0 * i, 20.0, 40.0, 22.0])
        lines.append({"patchline": {"destination": ["gen-code", i], "source": [f"gen-in{i + 1}", 0]}})
    for i in range(4):
        box(f"gen-out{i + 1}", maxclass="newobj", text=f"out {i + 1}", numinlets=1, numoutlets=0,
            patching_rect=[20.0 + 120.0 * i, 680.0, 40.0, 22.0])
        lines.append({"patchline": {"destination": [f"gen-out{i + 1}", 0], "source": ["gen-code", i]}})
    return {
        "fileversion": 1,
        "appversion": {"major": 8, "minor": 6, "revision": 0, "architecture": "x64", "modernui": 1},
        "classnamespace": "dsp.gen",
        "rect": [80.0, 80.0, 820.0, 740.0],
        "gridsize": [15.0, 15.0],
        "boxes": boxes,
        "lines": lines,
    }


def encode(patch: dict) -> bytes:
    """.amxd = 'ampf' chunk (device type 'aaaa' = audio effect), 'meta', then 'ptch' holding the patcher JSON."""
    body = json.dumps(patch, indent="\t").encode("utf-8") + b"\n\x00"
    header = b"ampf" + struct.pack("<I", 4) + b"aaaa" + b"meta" + struct.pack("<I", 4) + struct.pack("<I", 1)
    return header + b"ptch" + struct.pack("<I", len(body)) + body


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="fail if Cosmos.amxd does not match the sources")
    parser.add_argument("--out", type=Path, default=OUTPUT)
    args = parser.parse_args()

    data = encode(build_patch(GENEXPR.read_text(encoding="utf-8")))
    if args.check:
        if not args.out.exists() or args.out.read_bytes() != data:
            print(f"{args.out.name} is stale: run python3 {Path(__file__).relative_to(HERE.parents[2])}")
            return 1
        print(f"{args.out.name} is up to date")
        return 0
    args.out.write_bytes(data)
    print(f"wrote {args.out} ({len(data) // 1024} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
