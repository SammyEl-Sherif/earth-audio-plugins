// Duck: the curve editor (drag nodes, click to add, double-click to remove, Option-drag a segment to bend it)
// and the gain readout. Editing a stock shape copies it into the drawn "Custom" curve. ES5 (see aa_ui.js).

var Duck = {
    CUSTOM: 16,
    GAP: 0.004
};

Duck.nodes = function () {
    var preset = Math.round(AA.values.preset);
    if (preset < Duck.CUSTOM) {
        var src = AA.dev.data.presets[preset];
        var out = [];
        for (var i = 0; i < src.length; i++) out.push([src[i][0], src[i][1], src[i][2]]);
        return out;
    }
    var flat = AA.values.c && AA.values.c.length ? AA.values.c : AA.params.c.def;
    var n = Math.round(flat[0]);
    var nodes = [];
    for (var k = 0; k < n; k++) nodes.push([flat[1 + k * 3], flat[2 + k * 3], flat[3 + k * 3]]);
    if (nodes.length < 2) nodes = [[0, 0, 0], [1, 1, 0]];
    return nodes;
};

Duck.shape = function (t, bend) {
    if (bend === 0) return t;
    var p = Math.pow(2, Math.abs(bend) * 3);
    return bend > 0 ? Math.pow(t, p) : 1 - Math.pow(1 - t, p);
};

Duck.evaluate = function (nodes, x) {
    x = AA.clamp(x, 0, 1);
    var i = 0;
    while (i + 2 < nodes.length && x > nodes[i + 1][0]) i++;
    var a = nodes[i];
    var b = nodes[i + 1];
    var span = b[0] - a[0];
    var t = span > 0 ? (x - a[0]) / span : 1;
    return a[1] + (b[1] - a[1]) * Duck.shape(t, a[2]);
};

Duck.commit = function (nodes) {
    var flat = [nodes.length];
    for (var i = 0; i < AA.dev.data.nodes; i++) {
        if (i < nodes.length) flat.push(nodes[i][0], nodes[i][1], nodes[i][2]);
        else flat.push(0, 0, 0);
    }
    AA.set("c", flat);
    AA.set("preset", Duck.CUSTOM);
};

AA.actions.flip = function () {
    var src = Duck.nodes();
    var n = src.length;
    var out = [];
    for (var i = 0; i < n; i++) out.push([1 - src[n - 1 - i][0], src[n - 1 - i][1], 0]);
    for (i = 0; i + 1 < n; i++) out[i][2] = -src[n - 2 - i][2];
    Duck.commit(out);
};

AA.actions.invert = function () {
    var nodes = Duck.nodes();
    for (var i = 0; i < nodes.length; i++) nodes[i][1] = 1 - nodes[i][1];
    Duck.commit(nodes);
};

function duckArea(w) {
    var r = w.rect;
    return { x: r[0] + 6, y: r[1] + 6, w: r[2] - 12, h: r[3] - 12 };
}

function duckNodeAt(w, nodes, lx, ly) {
    var a = duckArea(w);
    var best = -1;
    var bestD = 81;
    for (var i = 0; i < nodes.length; i++) {
        var dx = a.x - w.rect[0] + nodes[i][0] * a.w - lx;
        var dy = a.y - w.rect[1] + (1 - nodes[i][1]) * a.h - ly;
        var d = dx * dx + dy * dy;
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}

function duckPoint(w, lx, ly) {
    var a = duckArea(w);
    return [AA.clamp((lx + w.rect[0] - a.x) / a.w, 0, 1), AA.clamp(1 - (ly + w.rect[1] - a.y) / a.h, 0, 1)];
}

AA.widget("duck.curve", {
    hover: true,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var a = duckArea(w);
        var nodes = Duck.nodes();
        var i;
        AA.fill(g, r[0], r[1], r[2], r[3], 6, t.well);
        AA.stroke(g, r[0], r[1], r[2], r[3], 6, t.line, 1);

        for (i = 1; i < 8; i++) {
            var gx = a.x + a.w * i / 8;
            AA.line(g, gx, a.y, gx, a.y + a.h, [1, 1, 1, i % 2 ? 0.025 : 0.05], 1);
        }
        AA.line(g, a.x, a.y + a.h / 2, a.x + a.w, a.y + a.h / 2, [1, 1, 1, 0.04], 1);

        var steps = 160;
        g.move_to(a.x, a.y + a.h);
        for (i = 0; i <= steps; i++) {
            var x = i / steps;
            g.line_to(a.x + x * a.w, a.y + (1 - Duck.evaluate(nodes, x)) * a.h);
        }
        g.line_to(a.x + a.w, a.y + a.h);
        g.close_path();
        AA.gradient(g, 0, a.y, 0, a.y + a.h, AA.alpha(t.accent, 0.32), AA.alpha(t.accent2, 0.03));
        g.fill();

        for (i = 0; i <= steps; i++) {
            var xx = i / steps;
            var py = a.y + (1 - Duck.evaluate(nodes, xx)) * a.h;
            if (i === 0) g.move_to(a.x, py);
            else g.line_to(a.x + xx * a.w, py);
        }
        g.set_line_width(2);
        g.set_line_join("round");
        AA.gradient(g, a.x, 0, a.x + a.w, 0, t.accent, t.accent2);
        g.stroke();

        var phase = AA.monitor("phase", -1);
        if (phase >= 0) {
            var px = a.x + phase * a.w;
            AA.gradient(g, 0, a.y, 0, a.y + a.h, AA.alpha(t.text, 0), AA.alpha(t.text, 0.35));
            g.rectangle(px - 0.5, a.y, 1, a.h);
            g.fill();
            var gain = AA.monitor("gain", 1);
            var gy = a.y + (1 - gain) * a.h;
            AA.circle(g, px, gy, 7, AA.alpha(t.accent, 0.18));
            AA.circle(g, px, gy, 3.5, t.text);
        }

        for (i = 0; i < nodes.length; i++) {
            var nx = a.x + nodes[i][0] * a.w;
            var ny = a.y + (1 - nodes[i][1]) * a.h;
            var hot = (AA.hover === w && w.hoverNode === i) || (AA.active === w && w.dragNode === i);
            if (hot) AA.circle(g, nx, ny, 8, AA.alpha(t.accent, 0.22));
            AA.circle(g, nx, ny, hot ? 4.5 : 3.5, t.well);
            g.ellipse(nx - (hot ? 4.5 : 3.5), ny - (hot ? 4.5 : 3.5), hot ? 9 : 7, hot ? 9 : 7);
            AA.color(g, hot ? t.text : t.accent);
            g.set_line_width(1.5);
            g.stroke();
        }

        var preset = Math.round(AA.values.preset);
        AA.text(g, AA.format("preset", preset), r[0] + 10, r[1] + 11, 8.5, t.dim, t.medium, "left");
        AA.text(g, "drag · click to add · double-click to remove · ⌥ drag to bend", r[0] + r[2] - 10, r[1] + r[3] - 9,
            7, AA.alpha(t.faint, AA.hover === w ? 1 : 0), t.medium, "right");
    },
    move: function (w, x, y) {
        var i = duckNodeAt(w, Duck.nodes(), x, y);
        if (w.hoverNode !== i) {
            w.hoverNode = i;
            AA.redraw();
        }
    },
    down: function (w, x, y, mods) {
        var nodes = Duck.nodes();
        var pt = duckPoint(w, x, y);
        w.nodes = nodes;
        w.dragNode = duckNodeAt(w, nodes, x, y);
        w.bendSeg = -1;
        if (mods.option && w.dragNode < 0) {
            var s = 0;
            while (s + 2 < nodes.length && pt[0] > nodes[s + 1][0]) s++;
            w.bendSeg = s;
            w.startY = y;
            w.startBend = nodes[s][2];
            return;
        }
        if (w.dragNode < 0 && nodes.length < AA.dev.data.nodes) {
            var at = 0;
            while (at + 1 < nodes.length && nodes[at + 1][0] < pt[0]) at++;
            if (pt[0] - nodes[at][0] > Duck.GAP && nodes[at + 1][0] - pt[0] > Duck.GAP) {
                nodes.splice(at + 1, 0, [pt[0], pt[1], nodes[at][2]]);
                w.dragNode = at + 1;
                Duck.commit(nodes);
            }
        }
    },
    drag: function (w, x, y) {
        var nodes = w.nodes;
        if (!nodes) return;
        if (w.bendSeg >= 0) {
            var a = nodes[w.bendSeg];
            var b = nodes[w.bendSeg + 1];
            var dir = b[1] >= a[1] ? 1 : -1;
            nodes[w.bendSeg][2] = AA.clamp(w.startBend + dir * (y - w.startY) / 60, -1, 1);
            Duck.commit(nodes);
            return;
        }
        var i = w.dragNode;
        if (i < 0) return;
        var pt = duckPoint(w, x, y);
        nodes[i][1] = pt[1];
        if (i > 0 && i < nodes.length - 1) {
            nodes[i][0] = AA.clamp(pt[0], nodes[i - 1][0] + Duck.GAP, nodes[i + 1][0] - Duck.GAP);
        }
        Duck.commit(nodes);
    },
    up: function (w) {
        w.nodes = null;
        w.dragNode = -1;
    },
    dbl: function (w, x, y) {
        var nodes = Duck.nodes();
        var i = duckNodeAt(w, nodes, x, y);
        if (i > 0 && i < nodes.length - 1) {
            nodes.splice(i, 1);
            Duck.commit(nodes);
        }
    }
});

AA.widget("duck.gain", {
    interactive: false,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var gain = AA.monitor("gain", 1);
        var db = gain > 0.0001 ? 20 * Math.log(gain) / Math.LN10 : -80;
        AA.spaced(g, "DUCKING", r[0], r[1] + 14, 7.5, t.faint, t.bold, 0.6);
        AA.text(g, db < -79 ? "-inf dB" : AA.number(db, 1) + " dB", r[0], r[1] + 30, 14, t.text, t.bold, "left");
        AA.fill(g, r[0], r[1] + 42, r[2], 4, 2, t.track);
        var depth = 1 - gain;
        if (depth > 0.005) AA.fillGradient(g, r[0], r[1] + 42, Math.max(4, r[2] * depth), 4, 2, t.accent, t.accent2, false);
    }
});

// Browser preview only (scripts/build_m4l.py --preview): fake the DSP's phase and gain.
AA.demo = function (sec) {
    var beats = [0.5, 1, 2, 4][Math.round(AA.values.division)];
    var phase = (sec * 2 / beats) % 1;
    var curve = Duck.evaluate(Duck.nodes(), phase);
    var mix = AA.values.drywet / 100;
    var gain = 1 - mix * (1 - curve);
    return { phase: phase, gain: gain, peakl: 0.6 * gain, peakr: 0.58 * gain };
};
