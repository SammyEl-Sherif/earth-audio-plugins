"""Live parameters of a device.

Each parameter becomes one hidden parameter object in the patcher (Push, automation and Live's mapping all see it)
and one entry in the UI's parameter table. `id` is the message selector used everywhere inside the patcher (the
gen~ `Param` name, the UI's routing key), so it must be a single lowercase word. `name` is the Live long name: keep
it stable, Live sets store values by it.
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass, field

# Live parameter unit styles (parameter_unitstyle).
UNIT_STYLES = {"int": 0, "float": 1, "ms": 2, "Hz": 3, "dB": 4, "%": 5, "pan": 6, "st": 7, "midi": 8, "custom": 9}

_ID = re.compile(r"^[a-z][a-z0-9_]*$")


def centre_exponent(lo: float, hi: float, centre: float) -> float:
    """Live maps x in 0..1 to lo + (hi - lo) * x^e; this e puts the knob's middle on `centre`."""
    return math.log((centre - lo) / (hi - lo)) / math.log(0.5)


@dataclass
class Param:
    name: str
    id: str
    short: str | None = None
    to_gen: bool = True  # wire the value into gen~ as `<id> <value>`

    kind = "param"

    def __post_init__(self) -> None:
        if not _ID.match(self.id):
            raise ValueError(f"param id {self.id!r} must be a lowercase word")
        if self.short is None:
            self.short = self.name

    @property
    def maxclass(self) -> str:
        return "live.numbox"

    def valueof(self) -> dict:
        raise NotImplementedError

    def ui(self) -> dict:
        """What the UI needs to draw, format and edit the value."""
        raise NotImplementedError


@dataclass
class Dial(Param):
    """Continuous value. unit: '%', 'Hz', 'dB', 'ms', 'st', 'int', 'float', or a printf format like '%.1f s'."""

    lo: float = 0.0
    hi: float = 100.0
    default: float = 0.0
    unit: str = "%"
    centre: float | None = None  # value at the knob's middle (skews the knob); None = linear
    bipolar: bool = False  # draw the arc from the middle

    kind = "dial"

    @property
    def exponent(self) -> float:
        return 1.0 if self.centre is None else round(centre_exponent(self.lo, self.hi, self.centre), 4)

    def valueof(self) -> dict:
        style = UNIT_STYLES.get(self.unit, UNIT_STYLES["custom"])
        v = {
            "parameter_longname": self.name, "parameter_shortname": self.short, "parameter_type": 1 if self.unit == "int" else 0,
            "parameter_mmin": self.lo, "parameter_mmax": self.hi, "parameter_initial": [self.default],
            "parameter_initial_enable": 1, "parameter_unitstyle": style,
        }
        if self.centre is not None:
            v["parameter_exponent"] = self.exponent
        if style == UNIT_STYLES["custom"]:
            v["parameter_units"] = self.unit
        return v

    def ui(self) -> dict:
        return {"kind": "dial", "name": self.name, "short": self.short, "lo": self.lo, "hi": self.hi,
                "def": self.default, "unit": self.unit, "exp": self.exponent, "bipolar": self.bipolar}


@dataclass
class Choice(Param):
    """One of a list (Push shows the item names)."""

    items: list[str] = field(default_factory=list)
    default: int = 0

    kind = "choice"

    def valueof(self) -> dict:
        return {
            "parameter_longname": self.name, "parameter_shortname": self.short, "parameter_type": 2,
            "parameter_enum": list(self.items), "parameter_mmax": len(self.items) - 1,
            "parameter_initial": [self.default], "parameter_initial_enable": 1, "parameter_unitstyle": 9,
        }

    def ui(self) -> dict:
        return {"kind": "choice", "name": self.name, "short": self.short, "items": list(self.items), "def": self.default}


@dataclass
class Toggle(Param):
    default: int = 0
    labels: tuple[str, str] = ("off", "on")

    kind = "toggle"

    def valueof(self) -> dict:
        return {
            "parameter_longname": self.name, "parameter_shortname": self.short, "parameter_type": 2,
            "parameter_enum": list(self.labels), "parameter_mmax": 1, "parameter_initial": [self.default],
            "parameter_initial_enable": 1,
        }

    def ui(self) -> dict:
        return {"kind": "toggle", "name": self.name, "short": self.short, "def": self.default}


@dataclass
class Button(Param):
    """Momentary command. gen~ gets a counter that increments per press (a change = a press)."""

    kind = "button"

    @property
    def maxclass(self) -> str:
        return "live.text"

    def valueof(self) -> dict:
        return {
            "parameter_longname": self.name, "parameter_shortname": self.short, "parameter_type": 2,
            "parameter_enum": ["off", "on"], "parameter_mmax": 1,
        }

    def ui(self) -> dict:
        return {"kind": "button", "name": self.name, "short": self.short}


@dataclass
class Blob(Param):
    """Non-automatable list of `size` numbers saved with the Live set (a `pattr` in blob mode), e.g. a drawn curve.

    The UI sends `<id> v0 v1 ...`. With to_gen, plain Max objects (no JS, so Push gets it too) forward element k to
    gen~ as `<id>k <value>`: declare `Param <id>0` .. `Param <id>{size-1}` in the GenExpr.
    """

    size: int = 0
    default: list[float] = field(default_factory=list)

    kind = "blob"

    @property
    def maxclass(self) -> str:
        return "pattr"

    def valueof(self) -> dict:
        v = {"parameter_longname": self.name, "parameter_shortname": self.short, "parameter_type": 3,
             "parameter_invisible": 1}
        if self.default:
            v.update(parameter_initial=list(self.default), parameter_initial_enable=1)
        return v

    def ui(self) -> dict:
        return {"kind": "blob", "name": self.name, "size": self.size, "def": list(self.default)}
