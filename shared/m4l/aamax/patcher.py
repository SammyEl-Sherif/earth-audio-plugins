"""Max patcher JSON: boxes, patch cords, and the gen~ sub-patcher."""

from __future__ import annotations

APPVERSION = {"major": 8, "minor": 6, "revision": 0, "architecture": "x64", "modernui": 1}


class Patcher:
    """Boxes laid out in columns of the (hidden) patching view; only the UI sits in presentation."""

    COLUMN_WIDTH = 190.0
    ROW_HEIGHT = 32.0

    def __init__(self) -> None:
        self.boxes: list[dict] = []
        self.lines: list[dict] = []
        self.parameters: dict[str, list] = {}
        self._next_id = 1
        self._row: dict[int, float] = {}

    def add(self, box: dict, column: int = 0) -> str:
        box_id = f"obj-{self._next_id}"
        self._next_id += 1
        y = self._row.get(column, 20.0)
        self._row[column] = y + self.ROW_HEIGHT
        box.setdefault("patching_rect", [20.0 + column * self.COLUMN_WIDTH, y, 170.0, 22.0])
        box["id"] = box_id
        self.boxes.append({"box": box})
        return box_id

    def obj(self, text: str, inlets: int, outlets: int, outlettype: list[str] | None = None, column: int = 0,
            **extra) -> str:
        """A plain object box (`newobj`), e.g. obj("prepend foo", 1, 1)."""
        return self.add(dict(maxclass="newobj", text=text, numinlets=inlets, numoutlets=outlets,
                             outlettype=outlettype if outlettype is not None else [""] * outlets, **extra), column)

    def message(self, text: str, column: int = 0) -> str:
        return self.add(dict(maxclass="message", text=text, numinlets=2, numoutlets=1, outlettype=[""]), column)

    def connect(self, src: str, outlet: int, dst: str, inlet: int) -> None:
        self.lines.append({"patchline": {"destination": [dst, inlet], "source": [src, outlet]}})

    def box(self, box_id: str) -> dict:
        return next(b["box"] for b in self.boxes if b["box"]["id"] == box_id)


def gen_patcher(code: str, inputs: int, outputs: int) -> dict:
    """gen~ sub-patcher: one codebox wired to `in 1..N` and `out 1..M`."""
    boxes, lines = [], []

    def box(box_id: str, **fields) -> None:
        boxes.append({"box": {"id": box_id, **fields}})

    box("gen-code", maxclass="codebox", code=code, fontface=0, fontname="<Monospaced>", fontsize=12.0,
        numinlets=inputs, numoutlets=outputs, outlettype=[""] * outputs, patching_rect=[20.0, 60.0, 760.0, 600.0])
    for i in range(inputs):
        box(f"gen-in{i + 1}", maxclass="newobj", text=f"in {i + 1}", numinlets=0, numoutlets=1, outlettype=[""],
            patching_rect=[20.0 + 120.0 * i, 20.0, 40.0, 22.0])
        lines.append({"patchline": {"destination": ["gen-code", i], "source": [f"gen-in{i + 1}", 0]}})
    for i in range(outputs):
        box(f"gen-out{i + 1}", maxclass="newobj", text=f"out {i + 1}", numinlets=1, numoutlets=0,
            patching_rect=[20.0 + 120.0 * i, 680.0, 40.0, 22.0])
        lines.append({"patchline": {"destination": [f"gen-out{i + 1}", 0], "source": ["gen-code", i]}})
    return {
        "fileversion": 1, "appversion": APPVERSION, "classnamespace": "dsp.gen", "rect": [80.0, 80.0, 820.0, 740.0],
        "gridsize": [15.0, 15.0], "boxes": boxes, "lines": lines,
    }
