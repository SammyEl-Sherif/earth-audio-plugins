"""Tests for the Max for Live builder and every device spec (run by scripts/check_repo.sh):

    python3 -m unittest discover -s shared/m4l/tests
"""

from __future__ import annotations

import importlib.util
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "shared" / "m4l"))
sys.path.insert(0, str(ROOT / "scripts"))

from aamax import Blob, Button, amxd, build  # noqa: E402
from aamax.device import UI_KIT, strip_comments, ui_script  # noqa: E402

import build_m4l  # noqa: E402

DEVICES = build_m4l.discover([])

# Max's jsui runs an old JavaScript engine: ES5 only.
ES6 = [
    (re.compile(r"=>"), "arrow function"),
    (re.compile(r"^\s*(let|const)\s", re.M), "let/const"),
    (re.compile(r"`"), "template string"),
    (re.compile(r"^\s*class\s", re.M), "class"),
    (re.compile(r"\.\.\.[A-Za-z_\[]"), "spread"),
    (re.compile(r"\b(Object\.assign|Array\.from|\.includes\(|\.find\()"), "ES2015+ library call"),
]


def boxes(patcher: dict) -> list[dict]:
    return [b["box"] for b in patcher["patcher"]["boxes"]]


class BuilderTests(unittest.TestCase):
    def test_container_round_trips(self):
        for device in DEVICES:
            with self.subTest(device=device.name):
                kind, patcher, files = amxd.decode(build(device))
                self.assertEqual(kind, device.kind)
                name, source = ui_script(device)
                self.assertEqual(files[name], source)
                self.assertEqual(len(files), 1 + len(device.files))
                jsui = [b for b in boxes(patcher) if b["maxclass"] == "jsui"]
                self.assertEqual([b["filename"] for b in jsui], [name])

    def test_frozen_layout_matches_max(self):
        data = build(DEVICES[0])
        self.assertEqual(data[:4], b"ampf")
        self.assertEqual(data[12:24], b"meta\x04\x00\x00\x00\x07\x00\x00\x00")
        self.assertEqual(data[24:28], b"ptch")
        self.assertEqual(data[32:36], b"mx@c")

    def test_every_parameter_has_one_live_object_and_banks_are_valid(self):
        for device in DEVICES:
            with self.subTest(device=device.name):
                _, patcher, _ = amxd.decode(build(device))
                params = patcher["patcher"]["parameters"]
                names = sorted(v[0] for k, v in params.items() if k.startswith("obj-"))
                self.assertEqual(names, sorted(p.name for p in device.params))
                for bank in params["parameterbanks"].values():
                    self.assertEqual(len(bank["parameters"]), 8, "Max writes 8 slots per bank")
                    self.assertEqual(bank["buttons"], ["-"] * 8)
                banked = {n for bank in device.banks for n in bank[1]} - {"-"}
                automatable = {p.name for p in device.params if not isinstance(p, Blob)}
                self.assertEqual(automatable - banked, set(), "every automatable parameter belongs in a Push bank")

    def test_router_covers_every_parameter(self):
        for device in DEVICES:
            with self.subTest(device=device.name):
                _, patcher, _ = amxd.decode(build(device))
                route = next(b for b in boxes(patcher) if b.get("text", "").startswith("route "))
                self.assertEqual(route["text"].split()[1:], [p.id for p in device.params] + ["gen", "ext"])

    def test_gen_params_match_spec(self):
        for device in DEVICES:
            if not device.gen:
                continue
            with self.subTest(device=device.name):
                code = strip_comments(device.gen_code())
                self.assertNotIn("// @insert", device.gen_code())
                for line in re.findall(r"^\s*Param\s+.*$", code, re.M):
                    self.assertRegex(line, r"min=.*max=")
                for literal in re.findall(r"\b\d{10,}\b", code):
                    self.assertLess(int(literal), 2 ** 31, "gen~ emits integer literals as C int")

    def test_gen_names_that_shadow_operators_are_rejected(self):
        # `Param mix` (mix is a gen~ operator) compiled to a silent device in Live.
        from aamax import Device, Dial
        from aamax.device import validate
        with tempfile.TemporaryDirectory() as tmp:
            Path(tmp, "x.genexpr").write_text("Param mix(0, min=0, max=100);\nout1 = in1 * mix;\nout2 = in2;\n")
            device = Device(name="X", slug="x", kind="audio_effect", description="", width=100, folder=Path(tmp),
                            params=[Dial("Mix", "mix")], banks=[("X", ["Mix"])], layout=[], gen="x.genexpr")
            with self.assertRaisesRegex(AssertionError, "shadow gen~ operators"):
                validate(device)

    def test_genexpr_lint_catches_what_broke_devices_in_live(self):
        from aamax.device import lint_genexpr
        ok = "Data d(4);\nsr = samplerate;\nf(a) {\n\treturn a > 0 ? a : 0;\n}\nx = peek(d, 0, 0);\nout1 = f(x) * sr;\n"
        self.assertEqual(lint_genexpr(ok), [])
        # Duck / Turtle Time: "codebox: variable sr is not defined"
        self.assertRegex(lint_genexpr(ok.replace("sr = samplerate;\n", ""))[0], r"undefined \['sr'\]")
        # Parrot: "codebox: variable peek_1120 is not defined"
        self.assertRegex(lint_genexpr(ok.replace("f(x)", "f(peek(d, 1, 0))"))[0], r"returns a ternary")

    def test_buttons_count_presses(self):
        for device in DEVICES:
            for param in device.params:
                if isinstance(param, Button):
                    _, patcher, _ = amxd.decode(build(device))
                    self.assertTrue(any(b.get("text", "").startswith("counter") for b in boxes(patcher)))
                    break

    def test_checked_in_devices_are_current(self):
        for device in DEVICES:
            with self.subTest(device=device.name):
                self.assertTrue(device.output.exists(), f"run python3 scripts/build_m4l.py {device.slug}")
                self.assertEqual(device.output.read_bytes(), build(device),
                                 f"{device.filename} is stale: run python3 scripts/build_m4l.py {device.slug}")


class UiScriptTests(unittest.TestCase):
    def scripts(self):
        yield UI_KIT
        for device in DEVICES:
            if device.ui_script and (device.folder / device.ui_script).exists():
                yield device.folder / device.ui_script
            for name in device.files:
                if name.endswith(".js"):
                    yield device.folder / name

    def test_es5_only(self):
        for path in self.scripts():
            text = re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8"))
            text = re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', text)
            for pattern, what in ES6:
                with self.subTest(script=str(path.relative_to(ROOT)), check=what):
                    self.assertIsNone(pattern.search(text), f"{what} is not ES5 (Max's jsui can't run it)")

    @unittest.skipUnless(shutil.which("node"), "node not installed")
    def test_ui_boots_paints_and_survives_interaction(self):
        smoke = Path(__file__).with_name("smoke.js")
        for device in DEVICES:
            with self.subTest(device=device.name), tempfile.TemporaryDirectory() as tmp:
                _, source = ui_script(device)
                script = Path(tmp) / "ui.js"
                script.write_bytes(source)
                result = subprocess.run(["node", str(smoke), str(script)], capture_output=True, text=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stderr or result.stdout)


if __name__ == "__main__":
    unittest.main()
