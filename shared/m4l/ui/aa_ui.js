// Animal Audio UI kit for Max for Live (jsui + mgraphics). One design system for every device.
//
// The build concatenates: this file, the device's own widgets (plugins/<slug>/m4l/ui.js, optional), then
// `AA.boot({...device table...})`. Max's jsui runs old JavaScript: ES5 only (var, function, no arrows,
// let/const, classes or template strings). shared/m4l/tests checks that.
//
// Messages in:   param <id> <value>     a parameter changed (from its hidden Live object)
//                mon <name> <values..>   live data from the DSP (meters, phase, state)
// Messages out:  <id> <value>            set a parameter (the patcher routes it to the Live object)
//                <id>                    press a button parameter
//                gen <name> <values..>   straight to gen~ (no parameter)
//                ext <name> <values..>   to device-specific wiring
//
// The UI never makes sound: everything audible goes through parameters and gen~, so the device works the
// same on Push 3 standalone, which doesn't show device UIs.

autowatch = 0;
inlets = 1;
outlets = 1;

mgraphics.init();
mgraphics.relative_coords = 0;
mgraphics.autofill = 0;

var AA = {
    types: {},
    params: {},
    values: {},
    mon: {},
    widgets: [],
    hover: null,
    active: null,
    dirty: true,
    dev: null,
    time: 0,
    HEADER: 26
};

// ---------------------------------------------------------------- theme

AA.theme = {
    bg: [0.071, 0.075, 0.09, 1],
    bgTop: [0.094, 0.098, 0.118, 1],
    card: [0.106, 0.114, 0.137, 1],
    cardTop: [0.122, 0.13, 0.157, 1],
    line: [1, 1, 1, 0.06],
    well: [0.055, 0.059, 0.071, 1],
    track: [0.18, 0.192, 0.231, 1],
    raised: [0.149, 0.157, 0.192, 1],
    text: [0.93, 0.937, 0.957, 1],
    dim: [0.6, 0.62, 0.68, 1],
    faint: [0.39, 0.41, 0.47, 1],
    ink: [0.047, 0.051, 0.063, 1],
    accent: [0.4, 0.86, 0.96, 1],
    accent2: [0.6, 0.51, 1.0, 1],
    warm: [1.0, 0.73, 0.36, 1],
    hot: [1.0, 0.37, 0.47, 1],
    good: [0.45, 0.9, 0.64, 1],
    bold: "Ableton Sans Bold",
    medium: "Ableton Sans Medium",
    light: "Ableton Sans Light"
};

AA.tone = function (name) {
    var t = AA.theme;
    if (name === "warm") return t.warm;
    if (name === "hot") return t.hot;
    if (name === "good") return t.good;
    if (name === "violet") return t.accent2;
    return t.accent;
};

AA.alpha = function (c, a) {
    return [c[0], c[1], c[2], c[3] * a];
};

AA.mix = function (a, b, t) {
    return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t];
};

AA.clamp = function (v, lo, hi) {
    return v < lo ? lo : (v > hi ? hi : v);
};

// ---------------------------------------------------------------- drawing helpers

AA.color = function (g, c) {
    g.set_source_rgba(c[0], c[1], c[2], c[3]);
};

AA.rrect = function (g, x, y, w, h, r) {
    g.new_path();
    if (r <= 0) {
        g.rectangle(x, y, w, h);
    } else {
        g.rectangle_rounded(x, y, w, h, r * 2, r * 2);
    }
};

AA.fill = function (g, x, y, w, h, r, c) {
    AA.rrect(g, x, y, w, h, r);
    AA.color(g, c);
    g.fill();
};

AA.stroke = function (g, x, y, w, h, r, c, width) {
    AA.rrect(g, x + 0.5, y + 0.5, w - 1, h - 1, r);
    AA.color(g, c);
    g.set_line_width(width || 1);
    g.stroke();
};

AA.gradient = function (g, x0, y0, x1, y1, c0, c1) {
    var p = g.pattern_create_linear(x0, y0, x1, y1);
    p.add_color_stop_rgba(0, c0[0], c0[1], c0[2], c0[3]);
    p.add_color_stop_rgba(1, c1[0], c1[1], c1[2], c1[3]);
    g.set_source(p);
};

AA.fillGradient = function (g, x, y, w, h, r, c0, c1, vertical) {
    AA.rrect(g, x, y, w, h, r);
    if (vertical) {
        AA.gradient(g, x, y, x, y + h, c0, c1);
    } else {
        AA.gradient(g, x, y, x + w, y, c0, c1);
    }
    g.fill();
};

AA.circle = function (g, cx, cy, r, c) {
    g.new_path();
    g.ellipse(cx - r, cy - r, r * 2, r * 2);
    AA.color(g, c);
    g.fill();
};

AA.line = function (g, x0, y0, x1, y1, c, width) {
    g.new_path();
    g.move_to(x0, y0);
    g.line_to(x1, y1);
    AA.color(g, c);
    g.set_line_width(width || 1);
    g.stroke();
};

// align: "left" | "center" | "right"; y is the vertical centre of the text.
AA.text = function (g, str, x, y, size, c, font, align) {
    str = String(str);
    g.select_font_face(font || AA.theme.medium);
    g.set_font_size(size);
    var m = g.text_measure(str);
    var e = g.font_extents();
    var tx = x;
    if (align === "center") tx = x - m[0] / 2;
    else if (align === "right") tx = x - m[0];
    g.move_to(tx, y + (e[0] - e[1]) / 2);
    AA.color(g, c);
    g.show_text(str);
    g.new_path();
    return m[0];
};

// Letter-spaced caps for labels and the wordmark.
AA.spaced = function (g, str, x, y, size, c, font, tracking, align) {
    str = String(str);
    g.select_font_face(font || AA.theme.bold);
    g.set_font_size(size);
    var widths = [];
    var total = 0;
    var i;
    for (i = 0; i < str.length; i++) {
        widths.push(g.text_measure(str.charAt(i))[0]);
        total += widths[i] + (i < str.length - 1 ? tracking : 0);
    }
    var tx = x;
    if (align === "center") tx = x - total / 2;
    else if (align === "right") tx = x - total;
    var e = g.font_extents();
    AA.color(g, c);
    for (i = 0; i < str.length; i++) {
        g.move_to(tx, y + (e[0] - e[1]) / 2);
        g.show_text(str.charAt(i));
        tx += widths[i] + tracking;
    }
    g.new_path();
    return total;
};

AA.fit = function (g, str, size, font, width) {
    g.select_font_face(font || AA.theme.medium);
    while (size > 6 && g.text_measure(String(str))[0] > width) size -= 0.5;
    return size;
};

// ---------------------------------------------------------------- parameters

AA.spec = function (id) {
    return AA.params[id];
};

AA.value = function (id) {
    return AA.values[id];
};

AA.norm = function (id, v) {
    var s = AA.params[id];
    if (!s) return 0;
    if (s.kind === "choice") return s.items.length > 1 ? v / (s.items.length - 1) : 0;
    if (s.kind === "toggle" || s.kind === "button") return v ? 1 : 0;
    var x = AA.clamp((v - s.lo) / (s.hi - s.lo), 0, 1);
    return s.exp === 1 ? x : Math.pow(x, 1 / s.exp);
};

AA.denorm = function (id, x) {
    var s = AA.params[id];
    x = AA.clamp(x, 0, 1);
    if (s.kind === "choice") return Math.round(x * (s.items.length - 1));
    if (s.kind === "toggle") return x >= 0.5 ? 1 : 0;
    var v = s.lo + (s.hi - s.lo) * (s.exp === 1 ? x : Math.pow(x, s.exp));
    return s.unit === "int" ? Math.round(v) : v;
};

AA.number = function (v, decimals) {
    var s = v.toFixed(decimals);
    return s === "-0" || s === "-0.0" || s === "-0.00" ? s.substring(1) : s;
};

AA.format = function (id, v) {
    var s = AA.params[id];
    if (!s) return "";
    if (v === undefined) v = s.def;
    if (s.kind === "choice") return s.items[Math.round(v)] || "";
    if (s.kind === "toggle") return v ? "On" : "Off";
    var u = s.unit;
    var a = Math.abs(v);
    if (u === "%") return AA.number(v, 0) + "%";
    if (u === "Hz") return a >= 1000 ? AA.number(v / 1000, a >= 10000 ? 1 : 2) + " kHz" : AA.number(v, a < 10 ? 2 : 0) + " Hz";
    if (u === "dB") return (v > 0.05 ? "+" : "") + AA.number(v, 1) + " dB";
    if (u === "ms") return a >= 1000 ? AA.number(v / 1000, 2) + " s" : AA.number(v, 0) + " ms";
    if (u === "st") return (v > 0.05 ? "+" : "") + AA.number(v, 1) + " st";
    if (u === "int") return String(Math.round(v));
    if (u === "float") return AA.number(v, 2);
    var m = /%(\.(\d))?([fd])/.exec(u);
    if (m) return u.replace(m[0], m[3] === "d" ? String(Math.round(v)) : AA.number(v, m[2] ? parseInt(m[2], 10) : 2));
    return AA.number(v, 2) + " " + u;
};

// Set a parameter from the UI: update locally (so the UI is instant), then tell the Live object.
AA.set = function (id, v) {
    var s = AA.params[id];
    if (!s) return;
    if (s.kind === "button") {
        outlet(0, id);
        return;
    }
    if (s.kind === "blob") {
        AA.values[id] = v;
        outlet(0, [id].concat(v));
        AA.redraw();
        return;
    }
    if (s.kind === "choice") v = AA.clamp(Math.round(v), 0, s.items.length - 1);
    else if (s.kind === "toggle") v = v ? 1 : 0;
    else v = AA.clamp(v, s.lo, s.hi);
    if (AA.values[id] === v) return;
    AA.values[id] = v;
    outlet(0, id, v);
    AA.redraw();
};

AA.gen = function (name) {
    outlet(0, ["gen"].concat(Array.prototype.slice.call(arguments)));
};

AA.ext = function (name) {
    outlet(0, ["ext"].concat(Array.prototype.slice.call(arguments)));
};

AA.redraw = function () {
    AA.dirty = true;
};

AA.monitor = function (name, fallback) {
    var v = AA.mon[name];
    return v === undefined ? fallback : v;
};

// ---------------------------------------------------------------- widgets

// AA.widget(type, { paint(g, w), down(w, x, y, mods), drag(w, x, y, mods), up(w), dbl(w), wheel(w, dy),
//                   hover: true, interactive: true }); x / y are local to the widget's rect.
AA.widget = function (type, proto) {
    AA.types[type] = proto;
};

AA.local = function (w, x, y) {
    return [x - w.rect[0], y - w.rect[1]];
};

function knobGeometry(w) {
    var s = w.spec.size || 38;
    var cx = w.rect[0] + w.rect[2] / 2;
    var cy = w.rect[1] + s / 2 + 1;
    return { cx: cx, cy: cy, r: s / 2 - 2 };
}

AA.drawArc = function (g, cx, cy, r, a0, a1, c0, c1, width) {
    if (a1 - a0 < 0.001) return;
    g.new_path();
    g.set_line_cap("round");
    g.set_line_width(width);
    g.arc(cx, cy, r, a0, a1);
    if (c1) AA.gradient(g, cx - r, cy, cx + r, cy, c0, c1);
    else AA.color(g, c0);
    g.stroke();
    g.set_line_cap("butt");
};

AA.ARC0 = Math.PI * 0.75;
AA.ARC = Math.PI * 1.5;

AA.widget("card", {
    interactive: false,
    paint: function (g, w) {
        var r = w.rect;
        var t = AA.theme;
        AA.fillGradient(g, r[0], r[1], r[2], r[3], 7, t.cardTop, t.card, true);
        AA.stroke(g, r[0], r[1], r[2], r[3], 7, t.line, 1);
        if (w.spec.title) AA.spaced(g, w.spec.title.toUpperCase(), r[0] + 9, r[1] + 10, 7.5, t.faint, t.bold, 1.1);
        if (w.spec.note) AA.text(g, w.spec.note, r[0] + r[2] - 9, r[1] + 10, 8, t.faint, t.medium, "right");
    }
});

AA.widget("knob", {
    hover: true,
    paint: function (g, w) {
        var t = AA.theme;
        var id = w.spec.param;
        var s = AA.params[id];
        var v = AA.values[id];
        var k = knobGeometry(w);
        var x = AA.norm(id, v);
        var live = AA.active === w;
        var hot = live || AA.hover === w;
        var tone = AA.tone(w.spec.tone);
        var tone2 = w.spec.tone ? AA.mix(tone, t.text, 0.25) : t.accent2;

        AA.drawArc(g, k.cx, k.cy, k.r, AA.ARC0, AA.ARC0 + AA.ARC, t.track, null, 3);
        var from = s.bipolar ? AA.ARC0 + AA.ARC / 2 : AA.ARC0;
        var to = AA.ARC0 + AA.ARC * x;
        var a0 = Math.min(from, to);
        var a1 = Math.max(from, to);
        AA.drawArc(g, k.cx, k.cy, k.r, a0, a1, AA.alpha(tone, hot ? 0.22 : 0.12), null, 7);
        AA.drawArc(g, k.cx, k.cy, k.r, a0, a1, tone, tone2, 3);

        var capR = k.r - 6;
        g.ellipse(k.cx - capR, k.cy - capR, capR * 2, capR * 2);
        AA.gradient(g, k.cx, k.cy - capR, k.cx, k.cy + capR, hot ? [0.2, 0.21, 0.255, 1] : t.raised, t.well);
        g.fill();
        g.ellipse(k.cx - capR + 0.5, k.cy - capR + 0.5, capR * 2 - 1, capR * 2 - 1);
        AA.color(g, [1, 1, 1, hot ? 0.1 : 0.06]);
        g.set_line_width(1);
        g.stroke();
        var a = AA.ARC0 + AA.ARC * x;
        AA.line(g, k.cx + Math.cos(a) * capR * 0.35, k.cy + Math.sin(a) * capR * 0.35,
            k.cx + Math.cos(a) * capR * 0.8, k.cy + Math.sin(a) * capR * 0.8, t.text, 2);
        AA.circle(g, k.cx + Math.cos(a) * k.r, k.cy + Math.sin(a) * k.r, 2, hot ? t.text : AA.alpha(t.text, 0.85));

        var below = k.cy + k.r + 8;
        var label = (w.spec.label || s.short).toUpperCase();
        var size = AA.fit(g, label, 7.5, t.bold, w.rect[2] + 6);
        AA.spaced(g, label, k.cx, below, size, hot ? t.dim : t.faint, t.bold, 0.6, "center");
        var text = AA.format(id, v);
        AA.text(g, text, k.cx, below + 11, AA.fit(g, text, 9, t.medium, w.rect[2] + 8), live ? tone : t.text, t.medium, "center");
    },
    down: function (w, x, y) {
        w.startY = y;
        w.startX = AA.norm(w.spec.param, AA.values[w.spec.param]);
    },
    drag: function (w, x, y, mods) {
        var dy = (w.startY - y) / (mods.shift ? 600 : 140);
        AA.set(w.spec.param, AA.denorm(w.spec.param, w.startX + dy));
    },
    dbl: function (w) {
        AA.set(w.spec.param, AA.params[w.spec.param].def);
    },
    wheel: function (w, dy) {
        var id = w.spec.param;
        AA.set(id, AA.denorm(id, AA.norm(id, AA.values[id]) + dy * 0.02));
    }
});

AA.widget("slider", {
    hover: true,
    paint: function (g, w) {
        var t = AA.theme;
        var id = w.spec.param;
        var r = w.rect;
        var x = AA.norm(id, AA.values[id]);
        var hot = AA.active === w || AA.hover === w;
        var tone = AA.tone(w.spec.tone);
        AA.spaced(g, (w.spec.label || AA.params[id].short).toUpperCase(), r[0], r[1] + 5, 7.5, hot ? t.dim : t.faint, t.bold, 0.6);
        AA.text(g, AA.format(id, AA.values[id]), r[0] + r[2], r[1] + 5, 9, AA.active === w ? tone : t.text, t.medium, "right");
        var ty = r[1] + r[3] - 6;
        AA.fill(g, r[0], ty - 2, r[2], 4, 2, t.track);
        if (x > 0) AA.fillGradient(g, r[0], ty - 2, Math.max(4, r[2] * x), 4, 2, tone, w.spec.tone ? tone : t.accent2);
        var tx = r[0] + AA.clamp(r[2] * x, 5, r[2] - 5);
        AA.circle(g, tx, ty, hot ? 6 : 5, AA.alpha(tone, 0.2));
        AA.circle(g, tx, ty, 4, t.text);
    },
    down: function (w, x) {
        AA.set(w.spec.param, AA.denorm(w.spec.param, x / w.rect[2]));
    },
    drag: function (w, x) {
        AA.set(w.spec.param, AA.denorm(w.spec.param, x / w.rect[2]));
    },
    dbl: function (w) {
        AA.set(w.spec.param, AA.params[w.spec.param].def);
    },
    wheel: function (w, dy) {
        var id = w.spec.param;
        AA.set(id, AA.denorm(id, AA.norm(id, AA.values[id]) + dy * 0.02));
    }
});

function segmentItems(w) {
    return w.spec.labels || AA.params[w.spec.param].items || ["Off", "On"];
}

function segmentIndex(w, x, y) {
    var items = segmentItems(w);
    var cols = w.spec.cols || items.length;
    var rows = Math.ceil(items.length / cols);
    var col = Math.floor(x / (w.rect[2] / cols));
    var row = Math.floor(y / (w.rect[3] / rows));
    return AA.clamp(row * cols + col, 0, items.length - 1);
}

// A row of options in one pill (labels default to the choice's items).
AA.widget("segment", {
    hover: true,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var items = segmentItems(w);
        var cur = Math.round(AA.values[w.spec.param]);
        var tone = AA.tone(w.spec.tone);
        var segW = r[2] / items.length;
        AA.fill(g, r[0], r[1], r[2], r[3], 6, t.well);
        AA.stroke(g, r[0], r[1], r[2], r[3], 6, t.line, 1);
        for (var i = 0; i < items.length; i++) {
            var sx = r[0] + segW * i;
            var on = i === cur;
            if (on) {
                AA.fillGradient(g, sx + 2, r[1] + 2, segW - 4, r[3] - 4, 4.5, tone, w.spec.tone ? AA.mix(tone, t.text, 0.2) : t.accent2, false);
            } else if (AA.hover === w && w.hoverIndex === i) {
                AA.fill(g, sx + 2, r[1] + 2, segW - 4, r[3] - 4, 4.5, AA.alpha(t.raised, 1));
            }
            var label = String(items[i]);
            var size = AA.fit(g, label, w.spec.size || 9, t.bold, segW - 6);
            AA.text(g, label, sx + segW / 2, r[1] + r[3] / 2, size, on ? t.ink : t.dim, t.bold, "center");
        }
    },
    move: function (w, x, y) {
        var i = segmentIndex(w, x, y);
        if (w.hoverIndex !== i) {
            w.hoverIndex = i;
            AA.redraw();
        }
    },
    down: function (w, x, y) {
        AA.set(w.spec.param, segmentIndex(w, x, y));
    },
    drag: function (w, x, y) {
        if (x >= 0 && x < w.rect[2]) AA.set(w.spec.param, segmentIndex(w, x, y));
    }
});

// A grid of chips for longer choice lists (modes, presets, patterns).
AA.widget("chips", {
    hover: true,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var items = segmentItems(w);
        var cols = w.spec.cols || items.length;
        var rows = Math.ceil(items.length / cols);
        var gap = w.spec.gap === undefined ? 3 : w.spec.gap;
        var cw = (r[2] - gap * (cols - 1)) / cols;
        var ch = (r[3] - gap * (rows - 1)) / rows;
        var cur = Math.round(AA.values[w.spec.param]);
        var tone = AA.tone(w.spec.tone);
        for (var i = 0; i < items.length; i++) {
            var cx = r[0] + (i % cols) * (cw + gap);
            var cy = r[1] + Math.floor(i / cols) * (ch + gap);
            var on = i === cur;
            var hov = AA.hover === w && w.hoverIndex === i;
            AA.fill(g, cx, cy, cw, ch, 4, on ? AA.alpha(tone, 0.16) : (hov ? t.raised : t.well));
            AA.stroke(g, cx, cy, cw, ch, 4, on ? AA.alpha(tone, 0.85) : t.line, 1);
            var label = String(items[i]);
            var size = AA.fit(g, label, w.spec.size || 8.5, t.bold, cw - 6);
            AA.text(g, label, cx + cw / 2, cy + ch / 2, size, on ? tone : (hov ? t.text : t.dim), t.bold, "center");
        }
    },
    move: function (w, x, y) {
        var i = segmentIndex(w, x, y);
        if (w.hoverIndex !== i) {
            w.hoverIndex = i;
            AA.redraw();
        }
    },
    down: function (w, x, y) {
        AA.set(w.spec.param, segmentIndex(w, x, y));
    }
});

// ‹ value › for a choice: click the arrows, drag, or scroll.
AA.widget("stepper", {
    hover: true,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var id = w.spec.param;
        var hot = AA.hover === w || AA.active === w;
        AA.fill(g, r[0], r[1], r[2], r[3], 6, t.well);
        AA.stroke(g, r[0], r[1], r[2], r[3], 6, hot ? AA.alpha(AA.tone(w.spec.tone), 0.6) : t.line, 1);
        var cy = r[1] + r[3] / 2;
        AA.chevron(g, r[0] + 9, cy, -1, hot ? t.text : t.faint);
        AA.chevron(g, r[0] + r[2] - 9, cy, 1, hot ? t.text : t.faint);
        var label = AA.format(id, AA.values[id]);
        AA.text(g, label, r[0] + r[2] / 2, cy, AA.fit(g, label, w.spec.size || 10, t.bold, r[2] - 30), t.text, t.bold, "center");
    },
    down: function (w, x, y) {
        w.startY = y;
        w.startV = AA.values[w.spec.param];
        w.moved = false;
    },
    drag: function (w, x, y) {
        var steps = Math.round((w.startY - y) / 12);
        if (steps !== 0) w.moved = true;
        if (w.moved) AA.set(w.spec.param, w.startV + steps);
    },
    up: function (w, x) {
        if (w.moved) return;
        var n = AA.params[w.spec.param].items.length;
        var v = Math.round(AA.values[w.spec.param]);
        if (x < w.rect[2] / 2) AA.set(w.spec.param, (v + n - 1) % n);
        else AA.set(w.spec.param, (v + 1) % n);
    },
    wheel: function (w, dy) {
        AA.set(w.spec.param, Math.round(AA.values[w.spec.param]) + (dy > 0 ? 1 : -1));
    }
});

AA.chevron = function (g, x, y, dir, c) {
    g.move_to(x - dir * 2, y - 4);
    g.line_to(x + dir * 2, y);
    g.line_to(x - dir * 2, y + 4);
    AA.color(g, c);
    g.set_line_width(1.5);
    g.set_line_cap("round");
    g.stroke();
    g.set_line_cap("butt");
};

// The pill shared by toggles and buttons. Custom widgets can call it with their own label and tone:
// AA.pill(g, w, lit, flash, "DUB", "warm").
AA.pill = function (g, w, on, flash, labelOverride, toneOverride) {
    var t = AA.theme;
    var r = w.rect;
    var tone = AA.tone(toneOverride || w.spec.tone);
    var hot = AA.hover === w;
    var lit = on || flash > 0;
    if (lit) {
        AA.fillGradient(g, r[0], r[1], r[2], r[3], 6, AA.alpha(tone, 0.28 + flash * 0.4), AA.alpha(tone, 0.14 + flash * 0.4), true);
        AA.stroke(g, r[0], r[1], r[2], r[3], 6, AA.alpha(tone, 0.9), 1);
    } else {
        AA.fillGradient(g, r[0], r[1], r[2], r[3], 6, hot ? [0.2, 0.21, 0.255, 1] : t.raised, t.card, true);
        AA.stroke(g, r[0], r[1], r[2], r[3], 6, hot ? [1, 1, 1, 0.12] : t.line, 1);
    }
    var label = labelOverride || w.spec.label || AA.params[w.spec.param].short.toUpperCase();
    var cx = r[0] + r[2] / 2;
    var cy = r[1] + r[3] / 2;
    if (w.spec.led !== false && r[2] > 40) {
        var size = AA.fit(g, label, w.spec.size || 8.5, t.bold, r[2] - 20);
        var tw = AA.spaced(g, label, -1000, -1000, size, t.text, t.bold, 0.7);
        var lx = cx - (tw + 9) / 2;
        if (lit) AA.circle(g, lx + 2.5, cy, 4.5, AA.alpha(tone, 0.25));
        AA.circle(g, lx + 2.5, cy, 2.5, lit ? tone : t.track);
        AA.spaced(g, label, lx + 9, cy, size, lit ? t.text : t.dim, t.bold, 0.7);
    } else {
        AA.spaced(g, label, cx, cy, AA.fit(g, label, w.spec.size || 8.5, t.bold, r[2] - 8), lit ? t.text : t.dim, t.bold, 0.7, "center");
    }
};

AA.widget("toggle", {
    hover: true,
    paint: function (g, w) {
        AA.pill(g, w, AA.values[w.spec.param] > 0.5, 0);
    },
    down: function (w) {
        AA.set(w.spec.param, AA.values[w.spec.param] > 0.5 ? 0 : 1);
    }
});

AA.widget("button", {
    hover: true,
    paint: function (g, w) {
        var lit = w.spec.litWhen ? AA.lit(w.spec.litWhen) : false;
        AA.pill(g, w, lit, w.flash || 0);
    },
    down: function (w) {
        AA.set(w.spec.param, 1);
        w.flash = 1;
        AA.animate(w);
    }
});

// A UI-only command (no parameter): runs AA.actions[spec.action]().
AA.actions = {};
AA.widget("action", {
    hover: true,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var hot = AA.hover === w;
        var tone = AA.tone(w.spec.tone);
        var flash = w.flash || 0;
        AA.fill(g, r[0], r[1], r[2], r[3], r[3] / 2, flash > 0 ? AA.alpha(tone, 0.2 + flash * 0.3) : (hot ? t.raised : AA.alpha(t.raised, 0.6)));
        AA.spaced(g, w.spec.label, r[0] + r[2] / 2, r[1] + r[3] / 2, AA.fit(g, w.spec.label, 7.5, t.bold, r[2] - 6),
            hot || flash > 0 ? t.text : t.dim, t.bold, 0.7, "center");
    },
    down: function (w) {
        var fn = AA.actions[w.spec.action];
        if (fn) fn();
        w.flash = 1;
        AA.animate(w);
    }
});

// litWhen: ["mon name", value, ...] lights a button while a monitor equals one of the values.
AA.lit = function (rule) {
    var v = Math.round(AA.monitor(rule[0], -1));
    for (var i = 1; i < rule.length; i++) if (v === rule[i]) return true;
    return false;
};

AA.animating = [];
AA.animate = function (w) {
    if (AA.animating.indexOf(w) < 0) AA.animating.push(w);
};

AA.widget("meter", {
    interactive: false,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var names = w.spec.mons || ["peakl", "peakr"];
        var gap = 2;
        var bw = (r[2] - gap * (names.length - 1)) / names.length;
        for (var i = 0; i < names.length; i++) {
            var x = r[0] + i * (bw + gap);
            var level = w.levels ? w.levels[i] : 0;
            AA.fill(g, x, r[1], bw, r[3], bw / 2, t.well);
            var h = r[3] * level;
            if (h > 1) {
                AA.rrect(g, x, r[1] + r[3] - h, bw, h, bw / 2);
                var p = g.pattern_create_linear(0, r[1] + r[3], 0, r[1]);
                p.add_color_stop_rgba(0, t.accent[0], t.accent[1], t.accent[2], 1);
                p.add_color_stop_rgba(0.7, t.accent2[0], t.accent2[1], t.accent2[2], 1);
                p.add_color_stop_rgba(1, t.hot[0], t.hot[1], t.hot[2], 1);
                g.set_source(p);
                g.fill();
            }
        }
    },
    tick: function (w) {
        var names = w.spec.mons || ["peakl", "peakr"];
        if (!w.levels) w.levels = [];
        var changed = false;
        for (var i = 0; i < names.length; i++) {
            var peak = AA.monitor(names[i], 0);
            var db = peak > 0.00001 ? 20 * Math.log(peak) / Math.LN10 : -100;
            var target = AA.clamp((db + 60) / 66, 0, 1);
            var cur = w.levels[i] || 0;
            var next = target > cur ? target : Math.max(target, cur - 0.04);
            if (Math.abs(next - cur) > 0.002) changed = true;
            w.levels[i] = next;
        }
        return changed;
    }
});

AA.widget("label", {
    interactive: false,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var c = w.spec.tone ? AA.tone(w.spec.tone) : t[w.spec.color || "dim"];
        AA.text(g, w.spec.text, w.spec.align === "center" ? r[0] + r[2] / 2 : (w.spec.align === "right" ? r[0] + r[2] : r[0]),
            r[1] + r[3] / 2, w.spec.size || 9, c, t[w.spec.font || "medium"], w.spec.align || "left");
    }
});

// Text from a monitor: { mon, items: [...] } picks a name by index, or { mon, format: "%.1f BPM" }.
AA.widget("readout", {
    interactive: false,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var v = AA.monitor(w.spec.mon, w.spec.fallback === undefined ? 0 : w.spec.fallback);
        var text;
        var tone = t.text;
        if (w.spec.items) {
            var i = AA.clamp(Math.round(v), 0, w.spec.items.length - 1);
            text = w.spec.items[i];
            if (w.spec.tones && w.spec.tones[i]) tone = AA.tone(w.spec.tones[i]);
        } else {
            var m = /%(\.(\d))?f/.exec(w.spec.format || "%.0f");
            text = (w.spec.format || "%.0f").replace(m[0], AA.number(v, m[2] ? parseInt(m[2], 10) : 0));
        }
        if (w.spec.pill) {
            var size = w.spec.size || 8;
            var tw = AA.spaced(g, text, -1000, -1000, size, tone, t.bold, 0.7);
            var px = w.spec.align === "right" ? r[0] + r[2] - tw - 14 : r[0];
            AA.fill(g, px, r[1], tw + 14, r[3], r[3] / 2, AA.alpha(tone, 0.12));
            AA.spaced(g, text, px + 7, r[1] + r[3] / 2, size, tone, t.bold, 0.7);
        } else {
            AA.text(g, text, w.spec.align === "right" ? r[0] + r[2] : r[0], r[1] + r[3] / 2, w.spec.size || 9, tone, t.medium, w.spec.align || "left");
        }
    }
});

// ---------------------------------------------------------------- frame

AA.paintFrame = function (g, W, H) {
    var t = AA.theme;
    g.rectangle(0, 0, W, H);
    AA.gradient(g, 0, 0, 0, H, t.bgTop, t.bg);
    g.fill();

    var y = AA.HEADER / 2 + 1;
    var gx = 14;
    g.ellipse(gx - 5, y - 5, 10, 10);
    AA.gradient(g, gx - 5, y - 5, gx + 5, y + 5, t.accent, t.accent2);
    g.fill();
    AA.circle(g, gx, y, 2.2, t.bg);
    var nameW = AA.spaced(g, AA.dev.title.toUpperCase(), gx + 11, y, 11, t.text, t.bold, 1.6);
    if (AA.dev.tagline) AA.text(g, AA.dev.tagline, gx + 11 + nameW + 10, y + 0.5, 8.5, t.faint, t.medium, "left");
    AA.spaced(g, "ANIMAL AUDIO", W - 10, y, 7, t.faint, t.bold, 1.4, "right");
    AA.line(g, 0, AA.HEADER + 0.5, W, AA.HEADER + 0.5, [1, 1, 1, 0.04], 1);
};

function paint() {
    var g = mgraphics;
    if (!AA.dev) return;
    var W = AA.dev.width;
    var H = AA.dev.height;
    AA.paintFrame(g, W, H);
    for (var i = 0; i < AA.widgets.length; i++) {
        var w = AA.widgets[i];
        g.save();
        w.type.paint(g, w);
        g.restore();
    }
    AA.dirty = false;
}

// ---------------------------------------------------------------- input

function mods(shift, option, cmd) {
    return { shift: !!shift, option: !!option, cmd: !!cmd };
}

AA.hit = function (x, y) {
    for (var i = AA.widgets.length - 1; i >= 0; i--) {
        var w = AA.widgets[i];
        if (w.type.interactive === false) continue;
        var r = w.rect;
        if (x >= r[0] && y >= r[1] && x < r[0] + r[2] && y < r[1] + r[3]) return w;
    }
    return null;
};

function onclick(x, y, but, cmd, shift, capslock, option, ctrl) {
    var w = AA.hit(x, y);
    AA.active = w;
    if (w && w.type.down) {
        var l = AA.local(w, x, y);
        w.type.down(w, l[0], l[1], mods(shift, option, cmd));
    }
    AA.redraw();
    mgraphics.redraw();
}
onclick.local = 1;

function ondrag(x, y, but, cmd, shift, capslock, option, ctrl) {
    var w = AA.active;
    if (!w) return;
    var l = AA.local(w, x, y);
    if (but === 0) {
        if (w.type.up) w.type.up(w, l[0], l[1]);
        AA.active = null;
    } else if (w.type.drag) {
        w.type.drag(w, l[0], l[1], mods(shift, option, cmd));
    }
    AA.redraw();
    mgraphics.redraw();
}
ondrag.local = 1;

function ondblclick(x, y, but, cmd, shift, capslock, option, ctrl) {
    var w = AA.hit(x, y);
    if (w && w.type.dbl) {
        var l = AA.local(w, x, y);
        w.type.dbl(w, l[0], l[1]);
    } else if (w && w.type.down) {
        var m = AA.local(w, x, y);
        w.type.down(w, m[0], m[1], mods(shift, option, cmd));
    }
    AA.redraw();
    mgraphics.redraw();
}
ondblclick.local = 1;

function onidle(x, y) {
    var w = AA.hit(x, y);
    if (w && w.type.move) {
        var l = AA.local(w, x, y);
        w.type.move(w, l[0], l[1]);
    }
    if (w !== AA.hover) {
        AA.hover = w;
        AA.redraw();
    }
}
onidle.local = 1;

function onidleout() {
    if (AA.hover) {
        AA.hover = null;
        AA.redraw();
    }
}
onidleout.local = 1;

function onwheel(x, y, wheelX, wheelY) {
    var w = AA.hit(x, y);
    if (w && w.type.wheel) {
        w.type.wheel(w, wheelY);
        mgraphics.redraw();
    }
}
onwheel.local = 1;

// ---------------------------------------------------------------- messages from the patcher

function param(id, v) {
    var s = AA.params[id];
    if (!s) return;
    var args = Array.prototype.slice.call(arguments, 1);
    if (s.kind === "blob") {
        AA.values[id] = args;
    } else if (s.kind === "button") {
        s.presses = (s.presses || 0) + 1;
    } else {
        if (AA.values[id] === v) return;
        AA.values[id] = v;
    }
    var listeners = AA.listeners[id];
    if (listeners) for (var i = 0; i < listeners.length; i++) listeners[i](AA.values[id]);
    AA.redraw();
}

function mon(name) {
    var args = Array.prototype.slice.call(arguments, 1);
    var v = args.length === 1 ? args[0] : args;
    var old = AA.mon[name];
    AA.mon[name] = v;
    if (args.length !== 1 || old !== v) AA.dirty = true;
}

AA.listeners = {};
// Run fn(value) whenever parameter `id` changes (from the UI, automation, Push or a set load).
AA.watch = function (id, fn) {
    if (!AA.listeners[id]) AA.listeners[id] = [];
    AA.listeners[id].push(fn);
};

function tick() {
    AA.time += 1;
    var i;
    for (i = 0; i < AA.widgets.length; i++) {
        var w = AA.widgets[i];
        if (w.type.tick && w.type.tick(w)) AA.dirty = true;
    }
    for (i = AA.animating.length - 1; i >= 0; i--) {
        var a = AA.animating[i];
        a.flash = Math.max(0, (a.flash || 0) - 0.12);
        if (a.flash <= 0) AA.animating.splice(i, 1);
        AA.dirty = true;
    }
    if (AA.dirty) mgraphics.redraw();
}
tick.local = 1;

AA.boot = function (table) {
    AA.dev = table;
    AA.params = table.params;
    for (var id in AA.params) {
        if (!AA.params.hasOwnProperty(id)) continue;
        var s = AA.params[id];
        if (s.kind === "dial" || s.kind === "choice" || s.kind === "toggle") AA.values[id] = s.def;
        if (s.kind === "blob") AA.values[id] = [];
    }
    AA.widgets = [];
    for (var i = 0; i < table.layout.length; i++) {
        var spec = table.layout[i];
        var type = AA.types[spec.type];
        if (!type) {
            post("Animal Audio UI: unknown widget " + spec.type + "\n");
            continue;
        }
        var w = { spec: spec, type: type, rect: spec.rect };
        if (type.init) type.init(w);
        AA.widgets.push(w);
    }
    if (AA.onboot) AA.onboot();
    AA.task = new Task(tick, this);
    AA.task.interval = 33;
    AA.task.repeat();
    mgraphics.redraw();
};
