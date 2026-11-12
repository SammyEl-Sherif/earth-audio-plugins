// Turtle Time: the window scope (input / output envelopes over the current musical window, NOW and READ heads), the
// 4 x 8 pattern matrix (rows = behaviour, columns = length; the active cell shows the window's progress) and the
// pitch DROP / KEEP switch (dimmed for 1x patterns). ES5 (see aa_ui.js).

var Turtle = {
    BINS: 96,
    COLS: 8
};

Turtle.pattern = function () {
    return AA.clamp(Math.round(AA.values.pattern), 0, AA.dev.data.table.length - 1);
};

// [rate, window beats, slice beats, mode] (mode 1 = repeat)
Turtle.spec = function (i) {
    return AA.dev.data.table[i === undefined ? Turtle.pattern() : i];
};

Turtle.slows = function (rate) {
    return rate > 0 && rate < 1;
};

Turtle.readout = function () {
    var i = Turtle.pattern();
    var row = Math.floor(i / Turtle.COLS);
    var d = AA.dev.data;
    var s = Turtle.spec(i);
    var text = d.groups[row] + " · " + d.details[row];
    if (Turtle.slows(s[0])) {
        if (AA.values.keeppitch > 0.5) text += " · PITCH KEPT";
        else text += s[0] >= 0.5 ? " · 1 OCTAVE DOWN" : " · 2 OCTAVES DOWN";
    }
    return text;
};

// ---------------------------------------------------------------- scope

function turtleArea(w) {
    var r = w.rect;
    return { x: r[0] + 6, y: r[1] + 6, w: r[2] - 12, h: r[3] - 26 };
}

function turtleEnvelope(g, a, bins, scale) {
    var n = bins.length;
    var mid = a.y + a.h / 2;
    var i;
    g.new_path();
    g.move_to(a.x, mid);
    for (i = 0; i < n; i++) g.line_to(a.x + (i + 0.5) / n * a.w, mid - Math.min(1, bins[i] * scale) * a.h / 2);
    g.line_to(a.x + a.w, mid);
    for (i = n - 1; i >= 0; i--) g.line_to(a.x + (i + 0.5) / n * a.w, mid + Math.min(1, bins[i] * scale) * a.h / 2);
    g.close_path();
}

function turtleHead(g, x, a, c, label, below) {
    AA.line(g, x, a.y, x, a.y + a.h, c, 1);
    var ty = below ? a.y + a.h - 5 : a.y + 5;
    var tw = AA.spaced(g, label, -1000, -1000, 6, c, AA.theme.bold, 0.6);
    var lx = x + tw + 6 > a.x + a.w ? x - tw - 8 : x + 2;
    AA.fill(g, lx, ty - 4.5, tw + 6, 9, 2, AA.alpha(AA.theme.well, 0.85));
    AA.spaced(g, label, lx + 3, ty, 6, c, AA.theme.bold, 0.6);
}

AA.widget("turtletime.scope", {
    interactive: false,
    init: function (w) {
        w.inBins = [];
        w.outBins = [];
        for (var i = 0; i < Turtle.BINS; i++) {
            w.inBins.push(0);
            w.outBins.push(0);
        }
        w.lastBin = -1;
        w.lastPattern = -1;
    },
    tick: function (w) {
        var p = Turtle.pattern();
        var i;
        if (p !== w.lastPattern) {
            for (i = 0; i < Turtle.BINS; i++) {
                w.inBins[i] = 0;
                w.outBins[i] = 0;
            }
            w.lastPattern = p;
            w.lastBin = -1;
        }
        if (AA.monitor("playing", 0) < 0.5) return false; // frozen while stopped, like the plugin
        var phase = AA.clamp(AA.monitor("phase", 0), 0, 0.99999);
        var bin = Math.floor(phase * Turtle.BINS);
        var inPk = AA.monitor("inpk", 0);
        var outPk = AA.monitor("outpk", 0);
        // fill every bin passed since the last tick (forward, across the window's wrap)
        var from = w.lastBin < 0 ? bin : (w.lastBin + 1) % Turtle.BINS;
        var guard = 0;
        i = from;
        while (guard < Turtle.BINS) {
            w.inBins[i] = inPk;
            w.outBins[i] = outPk;
            if (i === bin) break;
            i = (i + 1) % Turtle.BINS;
            guard++;
        }
        w.lastBin = bin;
        return true;
    },
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var a = turtleArea(w);
        var s = Turtle.spec();
        var i;
        AA.fill(g, r[0], r[1], r[2], r[3], 6, t.well);
        AA.stroke(g, r[0], r[1], r[2], r[3], 6, t.line, 1);

        // repeat: the captured slice
        if (s[3] > 0.5 && s[2] > 0) {
            var sw = a.w * Math.min(1, s[2] / s[1]);
            AA.fill(g, a.x, a.y, sw, a.h, 3, AA.alpha(t.accent2, 0.07));
        }

        // beat grid: beats for windows of a beat or more, else quarters of the window
        var lines = s[1] >= 1 ? Math.round(s[1]) : 4;
        if (lines > 16) lines = 16;
        for (i = 1; i < lines; i++) {
            var gx = a.x + a.w * i / lines;
            var strong = s[1] >= 4 && i % 4 === 0;
            AA.line(g, gx, a.y, gx, a.y + a.h, [1, 1, 1, strong ? 0.08 : 0.035], 1);
        }
        AA.line(g, a.x, a.y + a.h / 2, a.x + a.w, a.y + a.h / 2, [1, 1, 1, 0.04], 1);

        var peak = 0.05;
        for (i = 0; i < Turtle.BINS; i++) peak = Math.max(peak, w.inBins[i] || 0, w.outBins[i] || 0);
        var scale = 0.92 / peak;

        if (w.inBins) {
            turtleEnvelope(g, a, w.inBins, scale);
            AA.color(g, AA.alpha(t.text, 0.1));
            g.fill();
            turtleEnvelope(g, a, w.outBins, scale);
            AA.gradient(g, a.x, 0, a.x + a.w, 0, AA.alpha(t.accent, 0.55), AA.alpha(t.accent2, 0.55));
            g.fill();
        }

        var playing = AA.monitor("playing", 0) > 0.5;
        var phase = AA.clamp(AA.monitor("phase", 0), 0, 1);
        var marker = AA.clamp(AA.monitor("marker", 0), 0, 1);
        if (playing) turtleHead(g, a.x + marker * a.w, a, t.warm, "READ", true);
        turtleHead(g, a.x + phase * a.w, a, playing ? t.text : t.faint, "NOW", false);

        // footer: what the pattern does, or why it's dry
        var fy = r[1] + r[3] - 10;
        var text = Turtle.readout();
        AA.spaced(g, text, r[0] + 8, fy, AA.fit(g, text, 6.5, t.bold, r[2] - (playing ? 16 : 70)), t.dim, t.bold, 0.5);
        if (!playing) AA.spaced(g, "DRY", r[0] + r[2] - 8, fy, 6.5, t.faint, t.bold, 0.6, "right");
    }
});

// ---------------------------------------------------------------- pattern matrix

Turtle.LABEL_W = 58;
Turtle.TOP = 15;
Turtle.GAP = 3;

function turtleCell(w, x, y) {
    var r = w.rect;
    var rows = AA.dev.data.groups.length;
    var gw = r[2] - Turtle.LABEL_W;
    var gh = r[3] - Turtle.TOP;
    if (x < Turtle.LABEL_W - 4 || y < Turtle.TOP) return -1;
    var col = AA.clamp(Math.floor((x - Turtle.LABEL_W) / (gw / Turtle.COLS)), 0, Turtle.COLS - 1);
    var row = AA.clamp(Math.floor((y - Turtle.TOP) / (gh / rows)), 0, rows - 1);
    return row * Turtle.COLS + col;
}

AA.widget("turtletime.matrix", {
    hover: true,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var d = AA.dev.data;
        var rows = d.groups.length;
        var cur = Turtle.pattern();
        var curRow = Math.floor(cur / Turtle.COLS);
        var gap = Turtle.GAP;
        var gx = r[0] + Turtle.LABEL_W;
        var gy = r[1] + Turtle.TOP;
        var cw = (r[2] - Turtle.LABEL_W - gap * (Turtle.COLS - 1)) / Turtle.COLS;
        var ch = (r[3] - Turtle.TOP - gap * (rows - 1)) / rows;
        var playing = AA.monitor("playing", 0) > 0.5;
        var phase = AA.clamp(AA.monitor("phase", 0), 0, 1);
        var hov = AA.hover === w ? w.hoverIndex : -1;
        var size = 8;
        for (var k = 0; k < d.lengths.length; k++) size = Math.min(size, AA.fit(g, d.lengths[k], 8, t.bold, cw - 6));

        // current pattern name in the card's title row
        AA.text(g, AA.format("pattern", cur), r[0] + r[2], r[1] + 6, 8.5, t.dim, t.medium, "right");

        for (var row = 0; row < rows; row++) {
            var ry = gy + row * (ch + gap);
            var on = row === curRow;
            AA.spaced(g, d.groups[row], r[0], ry + ch / 2 - 4, 7.5, on ? t.text : t.dim, t.bold, 0.8);
            AA.spaced(g, d.details[row], r[0], ry + ch / 2 + 5.5, 5.5, on ? t.dim : t.faint, t.bold, 0.4);
            for (var col = 0; col < Turtle.COLS; col++) {
                var i = row * Turtle.COLS + col;
                var cx = gx + col * (cw + gap);
                var sel = i === cur;
                var hot = i === hov;
                AA.fill(g, cx, ry, cw, ch, 4, sel ? AA.alpha(t.accent, 0.16) : (hot ? t.raised : t.well));
                AA.stroke(g, cx, ry, cw, ch, 4, sel ? AA.alpha(t.accent, 0.85) : (hot ? [1, 1, 1, 0.12] : t.line), 1);
                if (sel && playing && phase > 0.001) {
                    AA.fillGradient(g, cx + 3, ry + ch - 4, Math.max(2, (cw - 6) * phase), 2, 1, t.accent, t.accent2, false);
                }
                var label = d.lengths[col];
                AA.text(g, label, cx + cw / 2, ry + ch / 2 - (sel && playing ? 1 : 0), size,
                    sel ? t.accent : (hot ? t.text : (on ? t.dim : AA.mix(t.dim, t.faint, 0.5))), t.bold, "center");
            }
        }
    },
    move: function (w, x, y) {
        var i = turtleCell(w, x, y);
        if (w.hoverIndex !== i) {
            w.hoverIndex = i;
            AA.redraw();
        }
    },
    down: function (w, x, y) {
        var i = turtleCell(w, x, y);
        if (i >= 0) AA.set("pattern", i);
    },
    drag: function (w, x, y) {
        var i = turtleCell(w, x, y);
        if (i >= 0 && x < w.rect[2] && y < w.rect[3]) AA.set("pattern", i);
    },
    wheel: function (w, dy) {
        AA.set("pattern", Turtle.pattern() + (dy > 0 ? 1 : -1));
    }
});

// ---------------------------------------------------------------- pitch DROP / KEEP (only slowed patterns change pitch)

AA.widget("turtletime.pitch", {
    hover: true,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var active = Turtle.slows(Turtle.spec()[0]);
        AA.spaced(g, "PITCH", r[0] + 1, r[1] - 7, 7.5, t.faint, t.bold, 0.6);
        AA.spaced(g, active ? "" : "1X", r[0] + r[2] - 1, r[1] - 7, 6.5, t.faint, t.bold, 0.6, "right");
        AA.types.segment.paint(g, w);
        if (!active) AA.fill(g, r[0], r[1], r[2], r[3], 6, AA.alpha(t.card, 0.6));
    },
    move: function (w, x, y) {
        AA.types.segment.move(w, x, y);
    },
    down: function (w, x, y) {
        AA.types.segment.down(w, x, y);
    }
});

// Browser preview only (scripts/build_m4l.py --preview): fake the DSP at 120 BPM over a choppy drum loop.
Turtle.demoLevel = function (beats) {
    var b = beats - Math.floor(beats);
    var e = Math.floor(beats * 2) % 2 ? 0.35 * Math.exp(-(b - 0.5) * 9) : 0.9 * Math.exp(-b * 7);
    return Math.max(0.04, e * (0.85 + 0.15 * Math.sin(beats * 1.7)));
};

AA.demo = function (sec) {
    var s = Turtle.spec();
    var beats = sec * 2;
    var win = s[1];
    var start = Math.floor(beats / win) * win;
    var into = beats - start;
    var phase = into / win;
    var marker;
    var readBeat;
    if (s[3] > 0.5) {
        var slice = into % s[2];
        marker = slice / win;
        readBeat = start + slice;
    } else {
        marker = s[0] >= 0 ? AA.clamp(s[0] * phase, 0, 1) : AA.clamp(1 + s[0] * phase, 0, 1);
        readBeat = start + s[0] * into;
    }
    var inPk = Turtle.demoLevel(beats);
    var mix = AA.values.drywet / 100;
    var outPk = inPk + (Turtle.demoLevel(readBeat) - inPk) * mix;
    return { playing: 1, bpm: 120, phase: phase, marker: marker, inpk: inPk, outpk: outPk,
        peakl: outPk * 0.8, peakr: outPk * 0.76 };
};
