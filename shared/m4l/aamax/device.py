"""A device spec and the patcher built from it.

Every Animal Audio device has the same shape, so a device's `device.py` only declares what differs:

    hidden parameter objects  --(prepend <id>)----------> gen~  (DSP)
           |   ^                                            |
           |   +---(route <ids>)---  jsui (the whole UI)  <-+-- snapshot~ / peakamp~ monitors (prepend mon <name>)
           +--------(prepend param <id>)--------------^

The jsui only draws and edits; all sound (and everything Push needs) lives in the parameters and gen~, so the
device behaves the same on Push 3 standalone, where device UIs are not shown.
"""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from . import amxd
from .params import Blob, Button, Param
from .patcher import APPVERSION, Patcher, gen_patcher

SHARED = Path(__file__).resolve().parents[1]
UI_KIT = SHARED / "ui" / "aa_ui.js"
DEVICE_HEIGHT = 169  # Live's device strip


@dataclass
class Monitor:
    """A gen~ output shown in the UI: `snapshot` samples it every `ms`; `peak` sends the peak since the last send."""

    name: str
    mode: str = "snapshot"
    ms: int = 33


@dataclass
class Context:
    """Handed to a device's `extra(ctx)` hook to add device-specific wiring (MIDI, js helpers, ...)."""

    patch: Patcher
    device: "Device"
    gen: str | None
    ui: str
    ui_router: str  # `route <ids...> gen ext`: outlet index of "ext" is `ext_outlet`
    ext_outlet: int
    params: dict[str, str]  # param id -> its Live object's box id
    sources: dict[str, str]  # param id -> box whose outlet 0 carries the value (a button's press counter)
    loadbang: str  # live.thisdevice (outlet 0 bangs once the device is ready)
    embedded: dict[str, str] = field(default_factory=dict)  # Device.files entry -> embedded file name

    def file(self, name: str) -> str:
        """The embedded name of one of Device.files, for box text like f"js {ctx.file('logic.js')}"."""
        return self.embedded[name]

    def to_ui(self, src: str, outlet: int, name: str) -> None:
        """Send whatever `src` outputs to the UI as `mon <name> ...`."""
        pre = self.patch.obj(f"prepend mon {name}", 1, 1, column=6)
        self.patch.connect(src, outlet, pre, 0)
        self.patch.connect(pre, 0, self.ui, 0)


@dataclass
class Device:
    name: str  # product name: "Duck" -> Duck.amxd
    slug: str  # plugins/<slug>
    kind: str  # audio_effect | instrument | midi_effect
    description: str
    width: int
    params: list[Param]
    banks: list[tuple[str, list[str]]]  # Push banks of 8 param names ("-" = empty slot)
    layout: list[dict]  # UI widgets (see aamax.ui)
    folder: Path
    tagline: str = ""  # short line next to the name in the header
    gen: str | None = None  # GenExpr file in `folder`
    gen_inputs: int = 2  # audio inputs the codebox reads as in1..inN (the beat ramp, if any, comes after)
    gen_outputs: int = 2  # audio outputs out1..outM (monitors come after)
    transport: bool = False  # Live's beat ramp on the next inlet + hostplaying / hostbpm / hostbeats params
    monitors: list[Monitor] = field(default_factory=list)
    meters: bool = True  # peak of out1/out2 to the UI as mon peakl / peakr
    ui_script: str | None = "ui.js"  # device widgets in `folder` (optional)
    ui_data: dict = field(default_factory=dict)  # extra table for the device's widgets (AA.dev.data)
    gen_inserts: dict[str, str] = field(default_factory=dict)  # `// @insert <key>` lines in the GenExpr -> code
    latency: int = 0  # samples of lookahead the DSP adds (Live's Defined Latency, for delay compensation)
    files: list[str] = field(default_factory=list)  # more files in `folder` to embed (e.g. a `js` helper); see Context.file
    extra: Callable[[Context], None] | None = None

    @property
    def filename(self) -> str:
        return f"{self.name.replace(' ', '')}.amxd"

    @property
    def output(self) -> Path:
        return self.folder / self.filename

    def param(self, name_or_id: str) -> Param:
        return next(p for p in self.params if name_or_id in (p.name, p.id))

    def gen_code(self) -> str:
        code = (self.folder / self.gen).read_text(encoding="utf-8")

        def insert(match: re.Match) -> str:
            key = match.group(2)
            if key not in self.gen_inserts:
                raise KeyError(f"{self.gen}: no gen_inserts[{key!r}]")
            return "\n".join(match.group(1) + line if line else "" for line in self.gen_inserts[key].splitlines())

        return re.sub(r"^([ \t]*)// @insert (\w+)[ \t]*$", insert, code, flags=re.M)


GEN_OPERATORS = frozenset(
    line.strip() for line in (Path(__file__).with_name("gen_operators.txt")).read_text(encoding="utf-8").splitlines()
    if line.strip() and not line.startswith("#"))


def strip_comments(code: str) -> str:
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", code, flags=re.S)


def _gen_io_count(code: str) -> tuple[int, int]:
    code = strip_comments(code)
    ins = [int(n) for n in re.findall(r"\bin(\d+)\b", code)]
    outs = [int(n) for n in re.findall(r"\bout(\d+)\s*=", code)]
    return max(ins, default=0), max(outs, default=0)


def embedded_files(device: Device) -> list[tuple[str, str, bytes]]:
    """(spec name, embedded name, contents) for Device.files. Names carry a content hash (see ui_script)."""
    out = []
    for name in device.files:
        data = (device.folder / name).read_bytes()
        stem, dot, ext = name.rpartition(".")
        out.append((name, f"aa_{device.slug}_{stem}_{hashlib.sha1(data).hexdigest()[:8]}.{ext}", data))
    return out


def ui_script(device: Device) -> tuple[str, bytes]:
    """The device's frozen UI file: the shared kit, the device's widgets, then the device table and boot call."""
    from .ui import device_table

    parts = [UI_KIT.read_text(encoding="utf-8")]
    if device.ui_script and (device.folder / device.ui_script).exists():
        parts.append((device.folder / device.ui_script).read_text(encoding="utf-8"))
    parts.append(f"AA.boot({device_table(device)});\n")
    source = "\n".join(parts).encode("utf-8")
    # Max caches frozen files by name across devices, so the name carries the content hash.
    digest = hashlib.sha1(source).hexdigest()[:8]
    return f"aa_{device.slug}_ui_{digest}.js", source


def build(device: Device) -> bytes:
    p = Patcher()
    validate(device)

    ui_name, ui_source = ui_script(device)
    ui = p.add(dict(maxclass="jsui", filename=ui_name, numinlets=1, numoutlets=1, outlettype=[""], border=0,
                    parameter_enable=0, presentation=1, varname="ui",
                    presentation_rect=[0.0, 0.0, float(device.width), float(DEVICE_HEIGHT)],
                    patching_rect=[20.0, 600.0, float(device.width), float(DEVICE_HEIGHT)]), column=0)

    loadbang = p.obj("live.thisdevice", 1, 3, ["bang", "int", "int"], column=1)
    init = p.obj("t b b", 1, 2, ["bang", "bang"], column=1)
    p.connect(loadbang, 0, init, 0)

    gen = None
    if device.gen:
        code = device.gen_code()
        n_in = device.gen_inputs + (1 if device.transport else 0)
        n_out = device.gen_outputs + len(device.monitors)
        gen = p.add(dict(maxclass="newobj", text="gen~", numinlets=max(n_in, 1), numoutlets=n_out,
                         outlettype=["signal"] * n_out, patcher=gen_patcher(code, n_in, n_out),
                         patching_rect=[20.0, 420.0, 320.0, 22.0]), column=0)
        if device.kind != "midi_effect":
            if device.gen_inputs:
                plugin = p.obj("plugin~", 2, 2, ["signal", "signal"], column=0)
                for ch in range(min(device.gen_inputs, 2)):
                    p.connect(plugin, ch, gen, ch)
            plugout = p.obj("plugout~", 2, 2, ["signal", "signal"], column=0,
                            patching_rect=[20.0, 480.0, 150.0, 22.0])
            for ch in range(min(device.gen_outputs, 2)):
                p.connect(gen, ch, plugout, ch)

        if device.transport:
            phasor = p.obj("plugphasor~", 1, 1, ["signal"], column=1)
            p.connect(phasor, 0, gen, device.gen_inputs)
            metro = p.obj("metro 10", 2, 1, ["bang"], column=2)
            sync = p.obj("plugsync~", 1, 9, ["int", "int", "int", "float", "list", "float", "float", "int", "int"],
                         column=2)
            p.connect(init, 0, metro, 0)
            p.connect(metro, 0, sync, 0)
            # plugsync~ outlets (Max reference): 0 transport on/off, 5 tempo in BPM, 6 position in beats (1 PPQ).
            for outlet, name in ((0, "hostplaying"), (5, "hostbpm"), (6, "hostbeats")):
                pre = p.obj(f"prepend {name}", 1, 1, column=2)
                p.connect(sync, outlet, pre, 0)
                p.connect(pre, 0, gen, 0)
            playing = p.obj("change", 1, 3, ["", "int", "int"], column=2)
            p.connect(sync, 0, playing, 0)
            pre = p.obj("prepend mon playing", 1, 1, column=2)
            p.connect(playing, 0, pre, 0)
            p.connect(pre, 0, ui, 0)

        for index, monitor in enumerate(device.monitors):
            outlet = device.gen_outputs + index
            if monitor.mode == "peak":
                probe = p.obj(f"peakamp~ {monitor.ms}", 2, 1, ["float"], column=6)
            else:
                probe = p.obj(f"snapshot~ {monitor.ms}", 2, 1, ["float"], column=6)
            p.connect(gen, outlet, probe, 0)
            pre = p.obj(f"prepend mon {monitor.name}", 1, 1, column=6)
            p.connect(probe, 0, pre, 0)
            p.connect(pre, 0, ui, 0)

        if device.meters and device.gen_outputs >= 2:
            for ch, name in ((0, "peakl"), (1, "peakr")):
                probe = p.obj("peakamp~ 40", 2, 1, ["float"], column=7)
                p.connect(gen, ch, probe, 0)
                pre = p.obj(f"prepend mon {name}", 1, 1, column=7)
                p.connect(probe, 0, pre, 0)
                p.connect(pre, 0, ui, 0)

    ids = [param.id for param in device.params]
    router = p.obj("route " + " ".join(ids + ["gen", "ext"]), 1, len(ids) + 3, column=3)
    p.connect(ui, 0, router, 0)
    if gen:
        p.connect(router, len(ids), gen, 0)

    boxes: dict[str, str] = {}
    sources: dict[str, str] = {}
    for index, param in enumerate(device.params):
        box_id = _param_box(p, param)
        boxes[param.id] = box_id
        p.parameters[box_id] = [param.name, param.short, 0]
        p.connect(router, index, box_id, 0)

        source = box_id
        if isinstance(param, Button):
            counter = p.obj("counter 1 1000000000", 5, 4, ["int", "", "", "int"], column=5)
            p.connect(box_id, 0, counter, 0)
            source = counter
        else:
            p.connect(init, 1, box_id, 0)  # re-send stored values once the device is ready
        sources[param.id] = source
        echo = p.obj(f"prepend param {param.id}", 1, 1, column=5)
        p.connect(source, 0, echo, 0)
        p.connect(echo, 0, ui, 0)
        if gen and param.to_gen and isinstance(param, Blob):
            funnel = p.obj("listfunnel", 1, 1, column=5)
            fmt = p.obj(f"sprintf {param.id}%ld %f", 2, 1, column=5)
            p.connect(source, 0, funnel, 0)
            p.connect(funnel, 0, fmt, 0)
            p.connect(fmt, 0, gen, 0)
        elif gen and param.to_gen:
            pre = p.obj(f"prepend {param.id}", 1, 1, column=5)
            p.connect(source, 0, pre, 0)
            p.connect(pre, 0, gen, 0)

    extras = embedded_files(device)
    if device.extra:
        device.extra(Context(patch=p, device=device, gen=gen, ui=ui, ui_router=router, ext_outlet=len(ids) + 1,
                             params=boxes, sources=sources, loadbang=loadbang, embedded={n: e for n, e, _ in extras}))

    files = [(ui_name, b"TEXT", ui_source)] + [(e, b"TEXT", data) for _, e, data in extras]
    return amxd.encode(device.kind, device.filename, _top_patcher(device, p, [f[0] for f in files]), files)


def _param_box(p: Patcher, param: Param) -> str:
    valueof = param.valueof()
    if isinstance(param, Blob):
        return p.add(dict(maxclass="newobj", text=f"pattr {param.id}", numinlets=1, numoutlets=3,
                          outlettype=["", "", ""], varname=param.id,
                          saved_attribute_attributes={"valueof": valueof},
                          saved_object_attributes={"parameter_enable": 1, "parameter_mappable": 0}), column=4)
    box = dict(maxclass=param.maxclass, numinlets=1, numoutlets=2, outlettype=["", "float"], parameter_enable=1,
               varname=param.id, saved_attribute_attributes={"valueof": valueof})
    if isinstance(param, Button):
        box.update(outlettype=["", ""], mode=0, text=param.short, texton=param.short)
    return p.add(box, column=4)


def _top_patcher(device: Device, p: Patcher, embedded: list[str]) -> dict:
    by_name = {param.name for param in device.params}
    parameters: dict = dict(sorted(p.parameters.items(), key=lambda kv: int(kv[0].split("-")[1])))
    parameters["parameterbanks"] = {
        # Max writes all 8 slots ("-" = empty); shorter lists log "parameter '<unknown>' not found" in Live.
        str(i): {"index": i, "name": bank, "parameters": list(names) + ["-"] * (8 - len(names)), "buttons": ["-"] * 8}
        for i, (bank, names) in enumerate(device.banks)
    }
    parameters["inherited_shortname"] = 1
    for _, names in device.banks:
        for n in names:
            assert n == "-" or n in by_name, f"{device.name}: bank lists unknown parameter {n!r}"

    return {
        "patcher": {
            "fileversion": 1, "appversion": APPVERSION, "classnamespace": "box",
            "rect": [100.0, 100.0, 1600.0, 900.0],
            "openrect": [0.0, 0.0, float(device.width), float(DEVICE_HEIGHT)],
            "openinpresentation": 1, "default_fontsize": 10.0, "default_fontface": 0,
            "default_fontname": "Arial Bold", "gridsize": [8.0, 8.0], "statusbarvisible": 0,
            "devicewidth": float(device.width), "description": device.description,
            "boxes": p.boxes, "lines": p.lines, "parameters": parameters,
            "dependency_cache": [{"name": n, "bootpath": "~/animal-audio/m4l", "type": "TEXT", "implicit": 1}
                                 for n in embedded],
            "latency": device.latency, "is_mpe": 0, "minimum_live_version": "", "minimum_max_version": "",
            "platform_compatibility": 0,
            "project": {
                "version": 1, "creationdate": 3590052786, "modificationdate": 3590052786,
                "viewrect": [0.0, 0.0, 300.0, 500.0], "autoorganize": 1, "hideprojectwindow": 1,
                "showdependencies": 1, "autolocalize": 0,
                "contents": {"patchers": {}, "code": {n: {"kind": "javascript"} for n in embedded}}, "layout": {},
                "searchpath": {}, "detailsvisible": 0, "amxdtype": amxd.AMXD_TYPE_CODES[device.kind], "readonly": 0,
                "devpathtype": 0, "devpath": ".", "sortmode": 0, "viewmode": 0, "includepackages": 0,
            },
            "autosave": 0, "bgcolor": [0.067, 0.071, 0.086, 1.0], "editing_bgcolor": [0.11, 0.118, 0.141, 1.0],
        }
    }


def validate(device: Device) -> None:
    """Catch spec mistakes before Max does (it fails silently)."""
    ids = [p.id for p in device.params]
    names = [p.name for p in device.params]
    assert len(set(ids)) == len(ids), f"{device.name}: duplicate param ids"
    assert len(set(names)) == len(names), f"{device.name}: duplicate param names"
    reserved = {"gen", "ext", "param", "mon", "hostplaying", "hostbpm", "hostbeats"}
    assert not reserved & set(ids), f"{device.name}: reserved ids {reserved & set(ids)}"
    for _, names_in_bank in device.banks:
        assert len(names_in_bank) <= 8, f"{device.name}: a Push bank holds 8 parameters"
    for widget in device.layout:
        for key in ("param", "params"):
            refs = widget.get(key)
            for ref in ([refs] if isinstance(refs, str) else refs or []):
                assert ref in ids, f"{device.name}: widget {widget['type']} refers to unknown param {ref!r}"
    if device.gen:
        code = device.gen_code()
        n_in, n_out = _gen_io_count(code)
        want_in = device.gen_inputs + (1 if device.transport else 0)
        want_out = device.gen_outputs + len(device.monitors)
        assert n_in == want_in, f"{device.gen}: uses in1..in{n_in}, the device wires {want_in} inlets"
        assert n_out == want_out, f"{device.gen}: assigns out1..out{n_out}, the device wires {want_out} outlets"
        code = strip_comments(code)
        declared = set(re.findall(r"^\s*Param\s+([a-z][a-z0-9_]*)", code, re.M))
        for param in device.params:
            if param.to_gen and isinstance(param, Blob):
                for k in range(param.size):
                    assert f"{param.id}{k}" in declared, f"{device.gen}: no `Param {param.id}{k}` for {param.name!r}"
            elif param.to_gen:
                assert param.id in declared, f"{device.gen}: no `Param {param.id}` for {param.name!r}"
        if device.transport:
            for name in ("hostplaying", "hostbpm", "hostbeats"):
                assert name in declared, f"{device.gen}: transport devices need `Param {name}`"
        names = re.findall(r"^\s*(?:Param|History|Data|Buffer|Delay)\s+([A-Za-z_]\w*)", code, re.M)
        names += re.findall(r"^([A-Za-z_]\w*)\s*\([^)]*\)\s*\{", code, re.M)
        clash = sorted(set(names) & GEN_OPERATORS)
        assert not clash, f"{device.gen}: {clash} shadow gen~ operators (gen~ won't compile; rename, e.g. mix -> drywet)"
        for line in re.findall(r"^\s*Param\s+.*$", code, re.M):
            assert "min=" in line and "max=" in line, f"{device.gen}: give every Param a min and max: {line.strip()}"
        problems = lint_genexpr(code)
        assert not problems, f"{device.gen}: " + "; ".join(problems)


_GEN_WORDS = {"if", "else", "for", "while", "return", "break", "continue", "Param", "History", "Data", "Buffer", "Delay",
              "min", "max", "interp", "index", "channels", "boundmode"}


def _call_args(code: str, open_paren: int) -> str:
    depth = 0
    for j in range(open_paren, len(code)):
        depth += {"(": 1, ")": -1}.get(code[j], 0)
        if depth == 0:
            return code[open_paren + 1:j]
    return code[open_paren + 1:]


def lint_genexpr(code: str) -> list[str]:
    """Mistakes gen~ only reports when Live loads the device (and then the device is silent). `code` has no comments."""
    code = re.sub(r'"[^"]*"', " ", code)
    problems = []
    funcs = {m.group(1): m for m in re.finditer(r"^([A-Za-z_]\w*)\s*\(([^)]*)\)\s*\{(.*?)^\}", code, re.M | re.S)
             if m.group(1) not in ("if", "for", "while")}

    # Every identifier must be defined somewhere: GenExpr has no `sr` (use `sr = samplerate;`, as Cosmos does).
    known = set(re.findall(r"^\s*(?:Param|History|Data|Buffer|Delay)\s+([A-Za-z_]\w*)", code, re.M))
    known |= set(re.findall(r"(?<![\w.])([A-Za-z_]\w*)\s*(?:=|\+=|-=|\*=|/=)(?!=)", code))
    known |= set(funcs) | GEN_OPERATORS | _GEN_WORDS
    known |= {a.strip() for m in funcs.values() for a in m.group(2).split(",") if a.strip()}
    undefined = sorted({u for u in re.findall(r"(?<![\w.])([A-Za-z_]\w*)\b", code)
                        if u not in known and not re.match(r"^(in|out)\d+$", u)})
    if undefined:
        problems.append(f"undefined {undefined} (GenExpr has no built-in sr: write `sr = samplerate;`)")

    # A helper that returns a ternary, called with peek() as an argument, fails to compile
    # ("variable peek_N is not defined"): read the value into a variable first.
    ternary = {name for name, m in funcs.items() if any("?" in r for r in re.findall(r"return([^;]*);", m.group(3)))}
    for name in ternary:
        for call in re.finditer(r"(?<![\w.])" + name + r"\s*\(", code):
            if call.start() == funcs[name].start():
                continue  # the definition itself
            if "peek(" in _call_args(code, call.end() - 1):
                problems.append(f"{name}() returns a ternary and is called with peek() in its arguments: "
                                "read the value into a variable first")
    return problems
