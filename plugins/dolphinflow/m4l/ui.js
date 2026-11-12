// Dolphin Flow: the flow display (chord name, voiced notes, voice-pitch ribbons), the glide and envelope previews,
// the eight arrangement slots and the Write Clip status. ES5 (see aa_ui.js). Nothing here makes sound: the chord
// logic is logic.js, the sound gen~; this only shows them and edits parameters.

var Flow = {
    HISTORY: 110, // ribbon points, 3.6 s at the 33 ms tick
    SPAN: 13 // semitones shown either side of the ribbons' centre
};

Flow.shape = function (i) {
    var s = AA.dev.data.shapes;
    return s[AA.clamp(i, 0, s.length - 1)];
};

Flow.chordName = function (root, shape) {
    if (root < 0) return "";
    return AA.dev.data.names[root % 12] + (shape >= 0 ? Flow.shape(shape)[1] : "");
};

// Live's note names: MIDI 60 is C3.
Flow.noteName = function (n) {
    return AA.dev.data.names[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 2);
};

Flow.slot = function (k) {
    var n = k + 1;
    return {
        root: Math.round(AA.values["s" + n + "root"]) - 1,
        shape: Math.round(AA.values["s" + n + "chord"]),
        len: Math.round(AA.values["s" + n + "len"])
    };
};

Flow.playingSlot = function () {
    return Math.round(AA.monitor("slot", -1));
};

Flow.pitches = function () {
    var out = [];
    for (var v = 0; v < 6; v++) {
        var p = AA.monitor("p" + v, -1);
        if (p >= 0) out.push(p);
    }
    return out;
};

// What is sounding: the loop's slot while it plays, else the last chord the keys played.
Flow.current = function () {
    var k = Flow.playingSlot();
    if (k >= 0) {
        var s = Flow.slot(k);
        return { root: s.root, shape: s.shape };
    }
    var c = AA.monitor("chord", null);
    if (!c || !c.length || c[1] < 0) return { root: -1, shape: 0 };
    return { root: c[1], shape: c[2] };
};

// The glide shape (DolphinFlowEngine glideShape): 0 even slide, 1 fast start easing into the note.
Flow.glide = function (t, curve) {
    var u = 1 - t;
    return t + curve * ((1 - u * u * u) - t);
};

Flow.well = function (g, r) {
    AA.fill(g, r[0], r[1], r[2], r[3], 6, AA.theme.well);
    AA.stroke(g, r[0], r[1], r[2], r[3], 6, AA.theme.line, 1);
};

// ---------------------------------------------------------------- flow display

AA.widget("dolphinflow.flow", {
    interactive: false,
    init: function (w) {
        w.hist = [];
        w.centre = 60;
    },
    tick: function (w) {
        var row = [];
        var sum = 0;
        var n = 0;
        for (var v = 0; v < 6; v++) {
            var p = AA.monitor("p" + v, -1);
            row.push(p);
            if (p >= 0) {
                sum += p;
                n++;
            }
        }
        w.hist.push(row);
        if (w.hist.length > Flow.HISTORY) w.hist.shift();
        if (n) w.centre += (sum / n - w.centre) * 0.08;
        return true;
    },
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        Flow.well(g, r);
        var plot = { x: r[0] + 22, y: r[1] + 40, w: r[2] - 30, h: r[3] - 46 };
        var lo = w.centre - Flow.SPAN;
        var hi = w.centre + Flow.SPAN;
        var yOf = function (p) {
            return plot.y + plot.h - (p - lo) / (hi - lo) * plot.h;
        };
        var i, v;

        // Envelope swell along the bottom.
        var env = AA.clamp(AA.monitor("env", 0), 0, 1);
        if (env > 0.01) {
            AA.gradient(g, 0, r[1] + r[3] - 34, 0, r[1] + r[3], AA.alpha(t.accent, 0), AA.alpha(t.accent, 0.1 * env));
            g.rectangle(r[0] + 1, r[1] + r[3] - 34, r[2] - 2, 33);
            g.fill();
        }

        // C lines with their names.
        for (var note = Math.ceil(lo); note <= Math.floor(hi); note++) {
            if (((note % 12) + 12) % 12 !== 0) continue;
            var cy = yOf(note);
            AA.line(g, plot.x, cy, plot.x + plot.w, cy, [1, 1, 1, 0.05], 1);
            AA.text(g, Flow.noteName(note), r[0] + 6, cy, 6.5, t.faint, t.medium, "left");
        }

        // One ribbon per voice, cyan (low) to violet (high).
        var count = 0;
        var last = w.hist.length ? w.hist[w.hist.length - 1] : [];
        for (v = 0; v < 6; v++) if (last[v] >= 0) count = v + 1;
        var step = plot.w / (Flow.HISTORY - 1);
        var x0 = plot.x + plot.w - step * (w.hist.length - 1);
        for (v = 0; v < 6; v++) {
            var colour = AA.mix(t.accent, t.accent2, count > 1 ? Math.min(1, v / (count - 1)) : 0);
            var drawing = false;
            var head = null;
            g.new_path();
            for (i = 0; i < w.hist.length; i++) {
                var p = w.hist[i][v];
                if (p < 0 || p < lo - 2 || p > hi + 2) {
                    drawing = false;
                    continue;
                }
                var px = x0 + step * i;
                var py = AA.clamp(yOf(p), plot.y - 4, plot.y + plot.h + 4);
                if (drawing) g.line_to(px, py);
                else g.move_to(px, py);
                drawing = true;
                head = [px, py, i === w.hist.length - 1];
            }
            g.set_line_join("round");
            g.set_line_cap("round");
            AA.color(g, AA.alpha(colour, 0.85));
            g.set_line_width(2);
            g.stroke();
            g.set_line_cap("butt");
            if (head && head[2]) {
                AA.circle(g, head[0], head[1], 5, AA.alpha(colour, 0.2));
                AA.circle(g, head[0], head[1], 2.5, t.text);
            }
        }

        // Chord name and voiced notes, over a fade so the ribbons pass under them.
        AA.gradient(g, 0, r[1] + 1, 0, r[1] + 40, AA.alpha(t.well, 1), AA.alpha(t.well, 0));
        g.rectangle(r[0] + 1, r[1] + 1, r[2] - 2, 39);
        g.fill();
        var cur = Flow.current();
        var sounding = AA.monitor("active", 0) > 0.5;
        var name = Flow.chordName(cur.root, cur.shape);
        var pitches = Flow.pitches();
        if (cur.root >= 0 && cur.shape < 0) {
            // A played voicing no shape names: its pitch classes.
            var pcs = [];
            for (i = 0; i < pitches.length; i++) {
                var pc = AA.dev.data.names[((Math.round(pitches[i]) % 12) + 12) % 12];
                if (pcs.indexOf(pc) < 0) pcs.push(pc);
            }
            name = pcs.join(" ");
        }
        if (!name) {
            AA.text(g, "Play a note", r[0] + 10, r[1] + 15, 12, t.faint, t.bold, "left");
            AA.text(g, "pads, keys or a clip", r[0] + 10, r[1] + 29, 7.5, t.faint, t.medium, "left");
        } else {
            var size = AA.fit(g, name, 17, t.bold, r[2] - 70);
            AA.text(g, name, r[0] + 10, r[1] + 15, size, sounding ? t.text : t.dim, t.bold, "left");
            var notes = [];
            for (i = 0; i < pitches.length; i++) notes.push(Flow.noteName(Math.round(pitches[i])));
            AA.text(g, sounding ? notes.join("  ") : "released", r[0] + 10, r[1] + 30, 7.5, t.dim, t.medium, "left");
        }
        var k = Flow.playingSlot();
        var tag = k >= 0 ? "LOOP " + (k + 1) : (AA.values.latch > 0.5 ? "LATCHED" : "");
        if (tag) {
            var tw = AA.spaced(g, tag, -1000, -1000, 6.5, t.text, t.bold, 0.7);
            var tone = k >= 0 ? t.accent : t.warm;
            AA.fill(g, r[0] + r[2] - tw - 20, r[1] + 8, tw + 12, 12, 6, AA.alpha(tone, 0.14));
            AA.spaced(g, tag, r[0] + r[2] - tw - 14, r[1] + 14, 6.5, tone, t.bold, 0.7);
        }
    }
});

// ---------------------------------------------------------------- glide preview

AA.widget("dolphinflow.glide", {
    interactive: false,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        Flow.well(g, r);
        var curve = AA.clamp(AA.values.curve / 100, 0, 1);
        var a = { x: r[0] + 8, y: r[1] + 8, w: r[2] - 16, h: r[3] - 16 };
        var steps = 40;
        var i;
        AA.line(g, a.x, a.y, a.x + a.w, a.y, [1, 1, 1, 0.05], 1);
        AA.line(g, a.x, a.y + a.h, a.x + a.w, a.y + a.h, [1, 1, 1, 0.05], 1);
        g.new_path();
        g.move_to(a.x, a.y + a.h);
        for (i = 0; i <= steps; i++) g.line_to(a.x + a.w * i / steps, a.y + a.h * (1 - Flow.glide(i / steps, curve)));
        g.line_to(a.x + a.w, a.y + a.h);
        g.close_path();
        AA.gradient(g, 0, a.y, 0, a.y + a.h, AA.alpha(t.accent, 0.22), AA.alpha(t.accent2, 0.02));
        g.fill();
        g.new_path();
        for (i = 0; i <= steps; i++) {
            var x = a.x + a.w * i / steps;
            var y = a.y + a.h * (1 - Flow.glide(i / steps, curve));
            if (i === 0) g.move_to(x, y);
            else g.line_to(x, y);
        }
        g.set_line_width(1.5);
        g.set_line_join("round");
        AA.gradient(g, a.x, 0, a.x + a.w, 0, t.accent, t.accent2);
        g.stroke();
        AA.circle(g, a.x, a.y + a.h, 2, t.dim);
        AA.circle(g, a.x + a.w, a.y, 2.5, t.text);
        AA.text(g, AA.format("glide", AA.values.glide), r[0] + r[2] - 7, r[1] + r[3] - 9, 7, t.faint, t.medium, "right");
    }
});

// ---------------------------------------------------------------- envelope preview (the MODE's envelope)

Flow.ENVELOPES = [
    // attack s, decay s, sustain, release s, label (DolphinFlowEngine::envelopeFor)
    [0.45, 0.5, 1.0, 1.8, "SWELL  ·  LEGATO"],
    [0.003, 0.28, 0.0, 0.15, "PLUCK  ·  RE-STRIKES"],
    [0.004, 1.4, 0.3, 0.6, "DECAY  ·  RE-STRIKES"]
];

Flow.envelopeAt = function (e, time, hold) {
    if (time < e[0]) return time / e[0];
    if (time < hold) return e[2] + (1 - e[2]) * Math.exp(-4.6 * (time - e[0]) / e[1]);
    var atRelease = e[2] + (1 - e[2]) * Math.exp(-4.6 * (hold - e[0]) / e[1]);
    return atRelease * Math.exp(-4.6 * (time - hold) / e[3]);
};

AA.widget("dolphinflow.env", {
    interactive: false,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        Flow.well(g, r);
        var e = Flow.ENVELOPES[AA.clamp(Math.round(AA.values.mode), 0, 2)];
        var a = { x: r[0] + 8, y: r[1] + 9, w: r[2] * 0.62 - 8, h: r[3] - 16 };
        var span = 3.2;
        var hold = 1.4;
        var steps = 80;
        var i;
        g.new_path();
        g.move_to(a.x, a.y + a.h);
        for (i = 0; i <= steps; i++) g.line_to(a.x + a.w * i / steps, a.y + a.h * (1 - Flow.envelopeAt(e, span * i / steps, hold)));
        g.line_to(a.x + a.w, a.y + a.h);
        g.close_path();
        AA.gradient(g, 0, a.y, 0, a.y + a.h, AA.alpha(t.accent, 0.22), AA.alpha(t.accent2, 0.02));
        g.fill();
        g.new_path();
        for (i = 0; i <= steps; i++) {
            var x = a.x + a.w * i / steps;
            var y = a.y + a.h * (1 - Flow.envelopeAt(e, span * i / steps, hold));
            if (i === 0) g.move_to(x, y);
            else g.line_to(x, y);
        }
        g.set_line_width(1.5);
        g.set_line_join("round");
        AA.gradient(g, a.x, 0, a.x + a.w, 0, t.accent, t.accent2);
        g.stroke();
        var hx = a.x + a.w * hold / span;
        AA.line(g, hx, a.y, hx, a.y + a.h, [1, 1, 1, 0.07], 1);

        var tx = r[0] + r[2] * 0.62 + 6;
        AA.spaced(g, AA.format("mode", AA.values.mode).toUpperCase(), tx, r[1] + 12, 8, t.text, t.bold, 0.8);
        AA.text(g, e[4], tx, r[1] + 23, 6.5, t.faint, t.medium, "left");
        var env = AA.clamp(AA.monitor("env", 0), 0, 1);
        var bw = r[0] + r[2] - 8 - tx;
        AA.fill(g, tx, r[1] + r[3] - 11, bw, 3, 1.5, t.track);
        if (env > 0.005) AA.fillGradient(g, tx, r[1] + r[3] - 11, Math.max(3, bw * env), 3, 1.5, t.accent, t.accent2, false);
    }
});

// ---------------------------------------------------------------- arrangement slots

// Click a slot to arm it: the next chord you play is written into it and the next slot arms. Drag up / down for
// the root, Option- or Shift-drag for the quality, the - / + corners for the length, double-click to empty it.
Flow.armed = -1;
Flow.lastChordSeq = -1;

function slotGeometry(w, k) {
    var r = w.rect;
    var gap = 4;
    var cw = (r[2] - gap * 3) / 4;
    var ch = (r[3] - gap) / 2;
    return { x: r[0] + (k % 4) * (cw + gap), y: r[1] + Math.floor(k / 4) * (ch + gap), w: cw, h: ch };
}

function slotAt(w, lx, ly) {
    for (var k = 0; k < 8; k++) {
        var c = slotGeometry(w, k);
        var x = lx + w.rect[0];
        var y = ly + w.rect[1];
        if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h) return { k: k, x: x - c.x, y: y - c.y, c: c };
    }
    return null;
}

// -1 shorter, +1 longer, 0 elsewhere (the bottom corners of a slot).
function slotLengthZone(hit) {
    if (!hit || hit.y < hit.c.h - 15) return 0;
    if (hit.x < 16) return -1;
    if (hit.x > hit.c.w - 16) return 1;
    return 0;
}

Flow.setSlot = function (k, field, v) {
    var n = k + 1;
    if (field === "root") AA.set("s" + n + "root", AA.clamp(v, -1, 11) + 1);
    else if (field === "shape") AA.set("s" + n + "chord", AA.clamp(v, 0, AA.dev.data.shapes.length - 1));
    else AA.set("s" + n + "len", AA.clamp(v, 0, AA.dev.data.lengths.length - 1));
};

AA.widget("dolphinflow.slots", {
    hover: true,
    tick: function (w) {
        // Capture: a chord played (struck) while a slot is armed is written into it.
        var c = AA.monitor("chord", null);
        if (!c || !c.length || c[0] === Flow.lastChordSeq) return false;
        Flow.lastChordSeq = c[0];
        if (Flow.armed < 0 || c[3] < 0.5 || c[1] < 0 || Flow.playingSlot() >= 0) return false;
        Flow.setSlot(Flow.armed, "root", c[1]);
        if (c[2] >= 0) Flow.setSlot(Flow.armed, "shape", c[2]);
        Flow.armed = Flow.armed < 7 ? Flow.armed + 1 : -1;
        return true;
    },
    paint: function (g, w) {
        var t = AA.theme;
        var playing = Flow.playingSlot();
        var prog = AA.clamp(AA.monitor("prog", 0), 0, 1);
        for (var k = 0; k < 8; k++) {
            var c = slotGeometry(w, k);
            var s = Flow.slot(k);
            var empty = s.root < 0;
            var isPlaying = k === playing;
            var armed = k === Flow.armed;
            var hot = AA.hover === w && w.hoverSlot === k;
            var tone = armed ? t.warm : t.accent;
            if (isPlaying) AA.fillGradient(g, c.x, c.y, c.w, c.h, 5, AA.alpha(t.accent, 0.2), AA.alpha(t.accent2, 0.1), true);
            else AA.fill(g, c.x, c.y, c.w, c.h, 5, hot ? t.raised : t.well);
            AA.stroke(g, c.x, c.y, c.w, c.h, 5, isPlaying || armed ? AA.alpha(tone, 0.9) : (hot ? [1, 1, 1, 0.12] : t.line), 1);
            if (isPlaying) AA.fillGradient(g, c.x + 4, c.y + c.h - 4, Math.max(2, (c.w - 8) * prog), 2, 1, t.accent, t.accent2, false);

            AA.text(g, String(k + 1), c.x + 6, c.y + 8, 7, armed ? t.warm : t.faint, t.bold, "left");
            var lenLabel = AA.dev.data.lengths[AA.clamp(s.len, 0, 7)];
            AA.text(g, lenLabel, c.x + c.w - 6, c.y + 8, AA.fit(g, lenLabel, 6.5, t.medium, c.w - 18), empty ? t.faint : t.dim,
                t.medium, "right");
            var name = empty ? "–" : Flow.chordName(s.root, s.shape);
            var size = AA.fit(g, name, 13, t.bold, c.w - 8);
            AA.text(g, name, c.x + c.w / 2, c.y + c.h / 2 + 2, size, empty ? t.faint : (isPlaying ? t.text : AA.mix(t.text, t.dim, 0.25)),
                t.bold, "center");
            if (armed) AA.spaced(g, "PLAY A CHORD", c.x + c.w / 2, c.y + c.h - 9, 5.5, t.warm, t.bold, 0.5, "center");
            else if (hot) {
                var zone = w.hoverZone;
                AA.text(g, "–", c.x + 8, c.y + c.h - 8, 9, zone < 0 ? t.text : t.faint, t.bold, "center");
                AA.text(g, "+", c.x + c.w - 8, c.y + c.h - 8, 9, zone > 0 ? t.text : t.faint, t.bold, "center");
            }
        }
    },
    move: function (w, x, y) {
        var hit = slotAt(w, x, y);
        var k = hit ? hit.k : -1;
        var zone = slotLengthZone(hit);
        if (k !== w.hoverSlot || zone !== w.hoverZone) {
            w.hoverSlot = k;
            w.hoverZone = zone;
            AA.redraw();
        }
    },
    down: function (w, x, y, mods) {
        var hit = slotAt(w, x, y);
        w.drag = null;
        if (!hit) return;
        var s = Flow.slot(hit.k);
        var zone = slotLengthZone(hit);
        if (zone !== 0) {
            Flow.setSlot(hit.k, "len", s.len + zone);
            return;
        }
        w.drag = { k: hit.k, y: y, root: s.root, shape: s.shape, quality: mods.option || mods.shift, moved: false };
    },
    drag: function (w, x, y) {
        var d = w.drag;
        if (!d) return;
        var steps = Math.round((d.y - y) / 8);
        if (steps === 0 && !d.moved) return;
        d.moved = true;
        if (d.quality) {
            if (d.root < 0) Flow.setSlot(d.k, "root", 9);
            Flow.setSlot(d.k, "shape", d.shape + steps);
        } else {
            Flow.setSlot(d.k, "root", AA.clamp(d.root + steps, -1, 11));
        }
    },
    up: function (w) {
        var d = w.drag;
        w.drag = null;
        if (!d || d.moved) return;
        Flow.armed = Flow.armed === d.k ? -1 : d.k;
        AA.redraw();
    },
    dbl: function (w, x, y) {
        var hit = slotAt(w, x, y);
        if (!hit || slotLengthZone(hit) !== 0) return;
        Flow.setSlot(hit.k, "root", -1);
        if (Flow.armed === hit.k) Flow.armed = -1;
    },
    wheel: function (w, dy) {
        var k = w.hoverSlot;
        if (k === undefined || k < 0) return;
        Flow.setSlot(k, "root", Flow.slot(k).root + (dy > 0 ? 1 : -1));
    }
});

// ---------------------------------------------------------------- Write Clip status, next to the card title

Flow.CLIP_TEXT = [null, ["SCENE ", "good"], ["NO EMPTY SLOT", "hot"], ["NOTHING TO WRITE", "hot"],
    ["API ERROR", "hot"]];

AA.widget("dolphinflow.status", {
    interactive: false,
    tick: function (w) {
        var c = AA.monitor("clip", null);
        if (c && c.length && c[0] !== w.seq) {
            w.seq = c[0];
            w.shown = 1;
            w.text = Flow.CLIP_TEXT[AA.clamp(Math.round(c[1]), 1, 4)];
            w.scene = c[2];
        }
        if (w.shown > 0) {
            w.shown -= 0.008;
            return true;
        }
        return false;
    },
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var text;
        var colour;
        if (w.shown > 0 && w.text) {
            text = w.text[0] + (w.text[1] === "good" ? w.scene : "");
            colour = AA.alpha(AA.tone(w.text[1]), Math.min(1, w.shown * 4));
        } else {
            var beats = 0;
            var used = 0;
            for (var k = 0; k < 8; k++) {
                var s = Flow.slot(k);
                if (s.root >= 0) {
                    used++;
                    beats += AA.dev.data.beats[AA.clamp(s.len, 0, 7)];
                }
            }
            text = !used ? "EMPTY" : (beats % 4 === 0 ? beats / 4 + (beats === 4 ? " BAR" : " BARS") : beats + " BEATS");
            colour = t.faint;
        }
        AA.spaced(g, text, r[0], r[1] + r[3] / 2, AA.fit(g, text, 6.5, t.bold, r[2]), colour, t.bold, 0.5);
    }
});

// Browser preview only (scripts/build_m4l.py --preview): the default loop playing, voices gliding between chords.
AA.demo = function (sec) {
    var voicings = [[52, 55, 60, 69], [52, 53, 60, 69], [52, 55, 60, 67], [50, 55, 59, 67]];
    var beats = sec * 2;
    var k = Math.floor(beats / 4) % 4;
    var into = (beats % 4) / 4;
    var from = voicings[(k + 3) % 4];
    var to = voicings[k];
    var g = Flow.glide(Math.min(1, into * 4 / 0.7), 0.5);
    var out = { slot: k, prog: into, env: 0.85 + 0.1 * Math.sin(sec * 2), active: 1, chord: [1, 9, 4, 0] };
    for (var v = 0; v < 6; v++) out["p" + v] = v < 4 ? from[v] + (to[v] - from[v]) * g : -1;
    out.peakl = 0.4 + 0.15 * Math.abs(Math.sin(sec * 3.1));
    out.peakr = 0.38 + 0.15 * Math.abs(Math.sin(sec * 2.7 + 1));
    return out;
};
