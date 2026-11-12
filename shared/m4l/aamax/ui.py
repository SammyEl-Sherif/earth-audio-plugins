"""UI layout: widgets the shared kit (shared/m4l/ui/aa_ui.js) draws, placed in device pixels.

The device strip is 169 px tall; the kit draws a 26 px header (name, tagline, brand), so content starts at y=CONTENT.
Widgets name parameters by id. Device-specific widgets (in the device's ui.js) use the same dict shape:
`widget("duck.curve", (x, y, w, h), param="curve", ...)`.
"""

from __future__ import annotations

import json

TOP = 32  # first content row (below the header)
BOTTOM = 163  # last content row
GAP = 6  # between cards
KNOB_W = 52  # knob cell width
KNOB_H = 62  # knob + label + value


def widget(type_: str, rect: tuple[float, float, float, float], **props) -> dict:
    return {"type": type_, "rect": [float(v) for v in rect], **props}


def card(rect, title: str = "", note: str = "") -> dict:
    return widget("card", rect, title=title, note=note)


def knob(param: str, x: float, y: float, label: str | None = None, tone: str | None = None, size: int = 36,
         width: float = KNOB_W) -> dict:
    props = {"param": param, "size": size}
    if label:
        props["label"] = label
    if tone:
        props["tone"] = tone
    return widget("knob", (x, y, width, size + 26), **props)


def knob_row(params: list[str], x: float, y: float, width: float = KNOB_W, **kw) -> list[dict]:
    return [knob(p, x + i * width, y, width=width, **kw) for i, p in enumerate(params)]


def slider(param: str, rect, label: str | None = None, tone: str | None = None) -> dict:
    return widget("slider", rect, param=param, **({"label": label} if label else {}), **({"tone": tone} if tone else {}))


def segment(param: str, rect, labels: list[str] | None = None, tone: str | None = None, size: float | None = None) -> dict:
    props: dict = {"param": param}
    if labels:
        props["labels"] = labels
    if tone:
        props["tone"] = tone
    if size:
        props["size"] = size
    return widget("segment", rect, **props)


def chips(param: str, rect, cols: int, labels: list[str] | None = None, tone: str | None = None,
          size: float | None = None) -> dict:
    props: dict = {"param": param, "cols": cols}
    if labels:
        props["labels"] = labels
    if tone:
        props["tone"] = tone
    if size:
        props["size"] = size
    return widget("chips", rect, **props)


def stepper(param: str, rect, tone: str | None = None) -> dict:
    return widget("stepper", rect, param=param, **({"tone": tone} if tone else {}))


def toggle(param: str, rect, label: str | None = None, tone: str | None = None, led: bool = True,
           size: float | None = None) -> dict:
    props: dict = {"param": param, "led": led}
    if size:
        props["size"] = size
    if label:
        props["label"] = label
    if tone:
        props["tone"] = tone
    return widget("toggle", rect, **props)


def button(param: str, rect, label: str | None = None, tone: str | None = None, lit_when: list | None = None,
           led: bool = True, size: float | None = None) -> dict:
    props: dict = {"param": param, "led": led}
    if size:
        props["size"] = size
    if label:
        props["label"] = label
    if tone:
        props["tone"] = tone
    if lit_when:
        props["litWhen"] = lit_when
    return widget("button", rect, **props)


def action(name: str, rect, label: str, tone: str | None = None) -> dict:
    """A UI-only button that runs AA.actions[name]() (e.g. flip a drawn curve); not a parameter."""
    return widget("action", rect, action=name, label=label, **({"tone": tone} if tone else {}))


def meter(rect, mons: list[str] | None = None) -> dict:
    return widget("meter", rect, mons=mons or ["peakl", "peakr"])


def label(rect, text: str, size: float = 9, color: str = "dim", align: str = "left", font: str = "medium") -> dict:
    return widget("label", rect, text=text, size=size, color=color, align=align, font=font)


def readout(rect, mon: str, items: list[str] | None = None, tones: list[str | None] | None = None,
            fmt: str | None = None, pill: bool = False, align: str = "left", size: float | None = None) -> dict:
    props: dict = {"mon": mon, "pill": pill, "align": align}
    if items:
        props["items"] = items
    if tones:
        props["tones"] = tones
    if fmt:
        props["format"] = fmt
    if size:
        props["size"] = size
    return widget("readout", rect, **props)


def device_table(device) -> str:
    from .device import DEVICE_HEIGHT

    table = {
        "title": device.name,
        "tagline": getattr(device, "tagline", "") or "",
        "slug": device.slug,
        "width": device.width,
        "height": DEVICE_HEIGHT,
        "params": {p.id: p.ui() for p in device.params},
        "layout": device.layout,
        "data": device.ui_data,
    }
    return json.dumps(table, separators=(",", ":"))
