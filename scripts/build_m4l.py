#!/usr/bin/env python3
"""Build the Max for Live devices (plugins/<slug>/m4l/device.py -> <Name>.amxd) with the shared kit in shared/m4l.

    python3 scripts/build_m4l.py                 # build every device
    python3 scripts/build_m4l.py duck cosmos     # just these
    python3 scripts/build_m4l.py --check         # exit 1 if a checked-in .amxd is stale (CI)
    python3 scripts/build_m4l.py --preview       # also write build/m4l-preview/index.html (UI in a browser)
"""

from __future__ import annotations

import argparse
import html
import importlib.util
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "shared" / "m4l"))

from aamax import build  # noqa: E402
from aamax.device import Device, ui_script  # noqa: E402

PREVIEW_SHIM = ROOT / "shared" / "m4l" / "ui" / "preview" / "shim.js"


def discover(slugs: list[str]) -> list[Device]:
    devices = []
    for spec in sorted(ROOT.glob("plugins/*/m4l/device.py")):
        slug = spec.parent.parent.name
        if slugs and slug not in slugs:
            continue
        module_spec = importlib.util.spec_from_file_location(f"m4l_{slug}", spec)
        module = importlib.util.module_from_spec(module_spec)
        module_spec.loader.exec_module(module)
        devices.append(module.DEVICE)
    missing = set(slugs) - {d.slug for d in devices}
    if missing:
        raise SystemExit(f"no plugins/<slug>/m4l/device.py for: {', '.join(sorted(missing))}")
    return devices


def preview_page(devices: list[Device], zoom: float = 2.0) -> str:
    shim = PREVIEW_SHIM.read_text(encoding="utf-8")
    frames = []
    for device in devices:
        _, source = ui_script(device)
        doc = f"""<!doctype html><html><head><style>
html,body{{margin:0;background:#0b0c0f;overflow:hidden}}canvas{{display:block}}</style></head><body>
<canvas id="c"></canvas><script>{shim}</script><script>
var zoom = {zoom};
var W = {device.width}, H = 169;
var c = document.getElementById("c");
var dpr = window.devicePixelRatio || 1;
c.width = W * zoom * dpr; c.height = H * zoom * dpr;
c.style.width = (W * zoom) + "px"; c.style.height = (H * zoom) + "px";
window.__zoom = zoom * dpr;
var draw = mountDevice(window, c, {json.dumps(source.decode("utf-8"))}, function (s) {{ console.log(s); }});
function pos(e) {{ var r = c.getBoundingClientRect(); return [(e.clientX - r.left) / zoom, (e.clientY - r.top) / zoom]; }}
var down = false;
c.addEventListener("mousedown", function (e) {{ down = true; var p = pos(e); onclick(p[0], p[1], 1, e.metaKey, e.shiftKey, 0, e.altKey, e.ctrlKey); }});
window.addEventListener("mousemove", function (e) {{ var p = pos(e); if (down) ondrag(p[0], p[1], 1, e.metaKey, e.shiftKey, 0, e.altKey, e.ctrlKey); else onidle(p[0], p[1]); }});
window.addEventListener("mouseup", function (e) {{ if (!down) return; down = false; var p = pos(e); ondrag(p[0], p[1], 0, e.metaKey, e.shiftKey, 0, e.altKey, e.ctrlKey); }});
c.addEventListener("mouseleave", function () {{ if (!down) onidleout(); }});
c.addEventListener("dblclick", function (e) {{ var p = pos(e); ondblclick(p[0], p[1], 1, e.metaKey, e.shiftKey, 0, e.altKey, e.ctrlKey); }});
c.addEventListener("wheel", function (e) {{ e.preventDefault(); var p = pos(e); onwheel(p[0], p[1], 0, -e.deltaY / 100); }}, {{passive: false}});
var t0 = Date.now();
setInterval(function () {{
  var t = (Date.now() - t0) / 1000;
  var demo = AA.demo ? AA.demo(t) : {{}};
  if (demo.peakl === undefined) {{ demo.peakl = 0.35 + 0.3 * Math.abs(Math.sin(t * 3.1)); demo.peakr = 0.33 + 0.3 * Math.abs(Math.sin(t * 2.7 + 1)); }}
  for (var k in demo) mon.apply(null, [k].concat(demo[k]));
}}, 33);
draw();
</script></body></html>"""
        frames.append(
            f'<section><h2>{html.escape(device.name)} <small>{html.escape(device.filename)} · {device.width}×169</small></h2>'
            f'<iframe srcdoc="{html.escape(doc, quote=True)}" width="{int(device.width * zoom)}" '
            f'height="{int(169 * zoom)}"></iframe></section>')
    return ("<!doctype html><html><head><meta charset='utf-8'><title>Animal Audio M4L preview</title><style>"
            "body{background:#0b0c0f;color:#9aa0ad;font:13px -apple-system,Inter,sans-serif;padding:24px}"
            "h2{font-size:13px;font-weight:600;color:#e8eaf0;margin:24px 0 8px}small{color:#5d6270;font-weight:500}"
            "iframe{border:0;border-radius:6px;display:block;box-shadow:0 10px 40px rgba(0,0,0,.5)}</style></head><body>"
            + "".join(frames) + "</body></html>")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("slugs", nargs="*", help="plugin slugs (default: all with an m4l/device.py)")
    parser.add_argument("--check", action="store_true", help="fail if a checked-in .amxd is stale")
    parser.add_argument("--preview", action="store_true", help="write build/m4l-preview/index.html")
    args = parser.parse_args()

    devices = discover(args.slugs)
    stale = []
    for device in devices:
        data = build(device)
        rel = device.output.relative_to(ROOT)
        if args.check:
            if not device.output.exists() or device.output.read_bytes() != data:
                stale.append(str(rel))
            continue
        device.output.write_bytes(data)
        print(f"wrote {rel} ({len(data) // 1024} KB)")
    if args.preview:
        out = ROOT / "build" / "m4l-preview" / "index.html"
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(preview_page(devices), encoding="utf-8")
        print(f"wrote {out.relative_to(ROOT)}")
    if stale:
        print("stale (run python3 scripts/build_m4l.py): " + ", ".join(stale))
        return 1
    if args.check:
        print(f"{len(devices)} Max for Live devices up to date")
    return 0


if __name__ == "__main__":
    sys.exit(main())
