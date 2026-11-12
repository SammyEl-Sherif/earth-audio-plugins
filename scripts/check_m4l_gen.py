#!/usr/bin/env python3
"""Compile every Max for Live device's gen~ code with the real Max compiler (macOS, needs Ableton Live installed).

gen~ only reports compile errors when Live loads a device, and then the device is just silent. This opens the Max
that ships inside Live as a separate, windowless-as-possible process (your Live session is untouched), loads each
device's gen~ in a throwaway patcher, starts DSP (that is when gen~ compiles), dumps the Max console to a file and
quits. Unlicensed standalone Max can't export or run gen~ audio, but it does compile and report errors.

    python3 scripts/check_m4l_gen.py              # every device
    python3 scripts/check_m4l_gen.py parrot duck  # some

Exit 1 if any device's gen~ fails to compile. Not run in CI (no Max there); run it before shipping a gen~ change.
"""

from __future__ import annotations

import argparse
import glob
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "shared" / "m4l"))
sys.path.insert(0, str(ROOT / "scripts"))

from aamax import amxd, build  # noqa: E402

import build_m4l  # noqa: E402

MAX_GLOBS = [
    "/Applications/Ableton Live 12*.app/Contents/App-Resources/Max/Max.app/Contents/MacOS/Max",
    "/Applications/Max.app/Contents/MacOS/Max",
]
# Standalone Max crashes on quit, so its crash recovery may try to reopen earlier check patchers; each device runs in
# its own temp folder that is gone by then.
# gen~ compiles when DSP starts; errors are in the console within a few seconds.
WAIT_MS = 5000
NOISE = ("doesn't understand", "cannot export", "dsp: ", "crash recovery")


def find_max() -> str | None:
    for pattern in MAX_GLOBS:
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[-1]
    return None


def check_patcher(gen_box: dict, console_file: Path) -> dict:
    boxes, lines = [], []

    def add(**box) -> str:
        box["id"] = f"o{len(boxes)}"
        box.setdefault("patching_rect", [10.0, 10.0 + 30 * len(boxes), 300.0, 22.0])
        boxes.append({"box": box})
        return box["id"]

    def msg(text: str) -> str:
        return add(maxclass="message", text=text, numinlets=2, numoutlets=1, outlettype=[""])

    def obj(text: str, inlets=1, outlets=1) -> str:
        return add(maxclass="newobj", text=text, numinlets=inlets, numoutlets=outlets, outlettype=[""] * outlets)

    def wire(a: str, b: str) -> None:
        lines.append({"patchline": {"source": [a, 0], "destination": [b, 0]}})

    start = obj("loadbang")
    console = obj("console")
    clear = msg("clear")
    dsp = msg(";dsp start")
    later = obj(f"delay {WAIT_MS}", 2)
    dump = msg(f"write {console_file}")
    end = obj(f"delay {WAIT_MS + 1500}", 2)
    quit_ = msg(";max quit")
    add(maxclass="newobj", text="gen~", numinlets=gen_box["numinlets"], numoutlets=gen_box["numoutlets"],
        outlettype=gen_box["outlettype"], patcher=gen_box["patcher"])
    for a, b in ((start, clear), (clear, console), (start, dsp), (start, later), (later, dump), (dump, console),
                 (start, end), (end, quit_)):
        wire(a, b)
    return {"patcher": {"fileversion": 1, "appversion": {"major": 9, "minor": 0, "revision": 0, "architecture": "x64",
                                                        "modernui": 1},
                        "classnamespace": "box", "rect": [40.0, 40.0, 400.0, 400.0], "boxes": boxes, "lines": lines}}


def compile_errors(max_bin: str, gen_box: dict, workdir: Path, name: str) -> list[str] | None:
    """gen~ errors for one patcher, or None if Max produced no console dump."""
    console_file = workdir / f"{name}_console.txt"
    patch_file = workdir / f"{name}.maxpat"
    patch_file.write_text(json.dumps(check_patcher(gen_box, console_file)), encoding="utf-8")
    proc = subprocess.Popen([max_bin, str(patch_file)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + WAIT_MS / 1000 + 30
    while proc.poll() is None and time.time() < deadline:
        time.sleep(0.5)
    if proc.poll() is None:
        proc.kill()
    if not console_file.exists():
        return None
    text = console_file.read_text(encoding="utf-8", errors="replace")
    return [line for line in text.splitlines() if line.strip() and not any(n in line for n in NOISE)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("slugs", nargs="*")
    args = parser.parse_args()
    max_bin = find_max()
    if not max_bin:
        print("Max not found (install Ableton Live Suite or Max); skipping")
        return 0
    failed = False
    for device in build_m4l.discover(args.slugs):
        if not device.gen:
            continue
        _, patcher, _ = amxd.decode(build(device))
        gen_box = next(b["box"] for b in patcher["patcher"]["boxes"] if b["box"].get("text") == "gen~")
        with tempfile.TemporaryDirectory(prefix="aa-gencheck-") as tmp:
            errors = compile_errors(max_bin, gen_box, Path(tmp), device.slug)
        if errors is None:
            print(f"?? {device.name}: Max didn't report (couldn't check)")
            failed = True
        elif errors:
            print(f"FAIL {device.name}:\n  " + "\n  ".join(errors))
            failed = True
        else:
            print(f"ok   {device.name}: gen~ compiles")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
