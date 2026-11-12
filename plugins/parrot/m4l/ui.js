// Parrot: the pitch trace (input vs output pitch over the last few seconds), the mode status line, the target /
// interval readout and the tube heat bar. All display only. ES5 (see aa_ui.js).

var Parrot = {
    NAMES: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"],
    INTERVALS: ["UNISON", "MINOR 2ND", "MAJOR 2ND", "MINOR 3RD", "MAJOR 3RD", "PERFECT 4TH", "TRITONE",
        "PERFECT 5TH", "MINOR 6TH", "MAJOR 6TH", "MINOR 7TH", "MAJOR 7TH", "OCTAVE"],
    POINTS: 150,   // trace length in UI ticks (33 ms): ~5 s
    SPAN: 24,      // semitones shown
    traceIn: [],
    traceOut: [],
    centre: 57,
    centreTarget: 57
};

Parrot.midi = function (hz) {
    return hz > 0 ? 69 + 12 * Math.log(hz / 440) / Math.LN2 : -1;
};

Parrot.noteName = function (m) {
    var r = Math.round(m);
    return Parrot.NAMES[((r % 12) + 12) % 12] + (Math.floor(r / 12) - 1);
};

Parrot.cents = function (m) {
    var c = Math.round((m - Math.round(m)) * 100);
    return (c > 0 ? "+" : "") + c + "c";
};

// PITCH as the engine applies it (dsp::appliedPitch): whole semitones unless FINE.
Parrot.pitch = function () {
    var p = AA.values.pitch || 0;
    return AA.values.fine > 0.5 ? p : Math.round(p);
};

Parrot.semis = function (v) {
    var whole = Math.abs(v - Math.round(v)) < 0.005;
    var s = whole ? String(Math.abs(Math.round(v))) : Math.abs(v).toFixed(2);
    return (v > 0.0005 ? "+" : (v < -0.0005 ? "-" : "")) + s + " ST";
};

Parrot.mode = function () {
    return Math.round(AA.values.mode || 0);
};

Parrot.held = function () {
    var h = AA.monitor("held", -1);
    return typeof h === "number" ? h : -1;
};

// ROBOT's target note: the MIDI key held, else ROBOT NOTE, plus PITCH.
Parrot.robotNote = function () {
    var h = Parrot.held();
    var base = h >= 0 ? h : AA.dev.data.lowest + Math.round(AA.values.note || 0);
    return base + Parrot.pitch();
};

Parrot.statusText = function () {
    var md = Parrot.mode();
    var p = Parrot.semis(Parrot.pitch());
    if (md === 0) return "SHIFT  ·  " + p;
    if (md === 1) return "SNAP  ·  CHROMATIC  ·  " + p;
    var h = Parrot.held();
    var src = h >= 0 ? "MIDI " + Parrot.noteName(h) : Parrot.noteName(AA.dev.data.lowest + Math.round(AA.values.note || 0));
    return "ROBOT  ·  " + src + "  ·  " + p;
};

// ---------------------------------------------------------------- status pill (card header)

AA.widget("parrot.status", {
    interactive: false,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var text = Parrot.statusText();
        var tone = Parrot.mode() === 2 && Parrot.held() >= 0 ? t.good : t.accent;
        var size = 7;
        var tw = AA.spaced(g, text, -1000, -1000, size, tone, t.bold, 0.6);
        var px = r[0] + r[2] - tw - 14;
        AA.fill(g, px, r[1], tw + 14, r[3], r[3] / 2, AA.alpha(tone, 0.12));
        AA.spaced(g, text, px + 7, r[1] + r[3] / 2, size, tone, t.bold, 0.6);
    }
});

// ---------------------------------------------------------------- pitch trace

Parrot.push = function (list, v) {
    list.push(v);
    if (list.length > Parrot.POINTS) list.shift();
};

Parrot.follow = function () {
    var lo = 1000;
    var hi = -1000;
    var i;
    for (i = 0; i < Parrot.traceIn.length; i++) {
        var a = Parrot.traceIn[i];
        var b = Parrot.traceOut[i];
        if (a >= 0) { lo = Math.min(lo, a); hi = Math.max(hi, a); }
        if (b >= 0) { lo = Math.min(lo, b); hi = Math.max(hi, b); }
    }
    if (hi < lo) return;
    var half = Parrot.SPAN / 2;
    var c = Parrot.centreTarget;
    if (hi > c + half - 1.5 || lo < c - half + 1.5) c = Math.round((lo + hi) / 2);
    Parrot.centreTarget = AA.clamp(c, 36 + half, 96 - half);
};

AA.widget("parrot.trace", {
    interactive: false,
    tick: function (w) {
        Parrot.push(Parrot.traceIn, Parrot.midi(AA.monitor("inhz", 0)));
        Parrot.push(Parrot.traceOut, Parrot.midi(AA.monitor("outhz", 0)));
        Parrot.follow();
        Parrot.centre += (Parrot.centreTarget - Parrot.centre) * 0.15;
        if (Math.abs(Parrot.centreTarget - Parrot.centre) < 0.01) Parrot.centre = Parrot.centreTarget;
        // Repaint while anything voiced is on screen (it scrolls); stay idle in silence.
        for (var i = 0; i < Parrot.traceOut.length; i++) {
            if (Parrot.traceOut[i] >= 0 || Parrot.traceIn[i] >= 0) return true;
        }
        return false;
    },
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var i;
        AA.fill(g, r[0], r[1], r[2], r[3], 6, t.well);
        AA.stroke(g, r[0], r[1], r[2], r[3], 6, t.line, 1);

        var gx = r[0] + 26;
        var gw = r[2] - 32;
        var gy = r[1] + 28;
        var gh = r[3] - 34;
        var top = Parrot.centre + Parrot.SPAN / 2;
        function yOf(m) {
            return gy + (top - m) / Parrot.SPAN * gh;
        }

        // Semitone rows, C rows labelled.
        var first = Math.ceil(top - Parrot.SPAN);
        for (var m = first; m <= top; m++) {
            var y = yOf(m);
            if (y < gy - 0.5 || y > gy + gh + 0.5) continue;
            var isC = ((m % 12) + 12) % 12 === 0;
            AA.line(g, gx, y, gx + gw, y, [1, 1, 1, isC ? 0.07 : 0.025], 1);
            if (isC) AA.text(g, Parrot.noteName(m), r[0] + 6, y, 7, t.faint, t.medium, "left");
        }

        // ROBOT's target note.
        if (Parrot.mode() === 2) {
            var ry = yOf(Parrot.robotNote());
            if (ry >= gy && ry <= gy + gh) {
                var tone = Parrot.held() >= 0 ? t.good : t.warm;
                for (var dx = 0; dx < gw; dx += 6) AA.line(g, gx + dx, ry, gx + Math.min(dx + 3, gw), ry, AA.alpha(tone, 0.55), 1);
            }
        }

        var n = Parrot.traceIn.length;
        var stepX = gw / (Parrot.POINTS - 1);
        var x0 = gx + gw - (n - 1) * stepX;
        Parrot.drawTrace(g, Parrot.traceIn, x0, stepX, yOf, gy, gy + gh, AA.alpha(t.dim, 0.55), null, 1.2);
        Parrot.drawTrace(g, Parrot.traceOut, x0, stepX, yOf, gy, gy + gh, t.accent, t.accent2, 2);

        var lastIn = n ? Parrot.traceIn[n - 1] : -1;
        var lastOut = n ? Parrot.traceOut[n - 1] : -1;
        if (lastOut >= 0) {
            var hy = AA.clamp(yOf(lastOut), gy, gy + gh);
            AA.circle(g, gx + gw, hy, 6, AA.alpha(t.accent2, 0.2));
            AA.circle(g, gx + gw, hy, 3, t.text);
        }

        // Header: IN note -> OUT note, output Hz.
        var hx = r[0] + 10;
        var hyy = r[1] + 13;
        var live = lastIn >= 0;
        AA.spaced(g, "IN", hx, hyy, 6.5, t.faint, t.bold, 0.8);
        var wIn = AA.text(g, live ? Parrot.noteName(lastIn) : "--", hx + 14, hyy, 12, live ? t.dim : t.faint, t.bold, "left");
        var ax = hx + 14 + wIn + 8;
        AA.text(g, "→", ax, hyy, 10, t.faint, t.medium, "left");
        AA.spaced(g, "OUT", ax + 16, hyy, 6.5, t.faint, t.bold, 0.8);
        var wOut = AA.text(g, lastOut >= 0 ? Parrot.noteName(lastOut) : "--", ax + 36, hyy, 12, lastOut >= 0 ? t.text : t.faint, t.bold, "left");
        if (lastOut >= 0) AA.text(g, Parrot.cents(lastOut), ax + 36 + wOut + 5, hyy + 1, 7.5, t.faint, t.medium, "left");
        var hz = AA.monitor("outhz", 0);
        AA.text(g, hz > 0 ? AA.number(hz, 1) + " Hz" : "LISTENING", r[0] + r[2] - 10, hyy, 8, hz > 0 ? t.dim : t.faint, t.medium, "right");
    }
});

Parrot.drawTrace = function (g, list, x0, stepX, yOf, ylo, yhi, c0, c1, width) {
    var drawing = false;
    var any = false;
    g.new_path();
    for (var i = 0; i < list.length; i++) {
        var v = list[i];
        if (v < 0) {
            drawing = false;
            continue;
        }
        var x = x0 + i * stepX;
        var y = AA.clamp(yOf(v), ylo, yhi);
        if (!drawing) {
            g.move_to(x, y);
            drawing = true;
        } else {
            g.line_to(x, y);
        }
        any = true;
    }
    if (!any) return;
    g.set_line_width(width);
    g.set_line_join("round");
    g.set_line_cap("round");
    if (c1) AA.gradient(g, x0, 0, x0 + stepX * list.length, 0, c0, c1);
    else AA.color(g, c0);
    g.stroke();
    g.set_line_cap("butt");
};

// ---------------------------------------------------------------- target / interval readout (Mode card)

AA.widget("parrot.target", {
    interactive: false,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var md = Parrot.mode();
        var label;
        var big;
        var small;
        var tone = t.faint;
        if (md === 2) {
            var h = Parrot.held();
            label = "ROBOT NOTE";
            big = Parrot.noteName(Parrot.robotNote());
            small = h >= 0 ? "MIDI KEY HELD" : "OR HOLD A MIDI KEY";
            tone = h >= 0 ? t.good : t.faint;
        } else if (md === 1) {
            var out = Parrot.midi(AA.monitor("outhz", 0));
            label = "SNAPPED TO";
            big = out >= 0 ? Parrot.noteName(out) : "--";
            small = "NEAREST SEMITONE";
        } else {
            var p = Parrot.pitch();
            var whole = AA.clamp(Math.round(p), -12, 12);
            label = "INTERVAL";
            big = Parrot.semis(p).replace(" ST", " st");
            small = Parrot.INTERVALS[Math.abs(whole)] + (whole > 0 ? " UP" : (whole < 0 ? " DOWN" : ""));
        }
        AA.spaced(g, label, r[0], r[1] + 8, 7, t.faint, t.bold, 0.6);
        AA.text(g, big, r[0], r[1] + 25, AA.fit(g, big, 15, t.bold, r[2]), t.text, t.bold, "left");
        AA.spaced(g, small, r[0], r[1] + 42, AA.fit(g, small, 6.5, t.bold, r[2] - 6), tone, t.bold, 0.4);
    }
});

// ---------------------------------------------------------------- tube heat (Output card)

AA.widget("parrot.tube", {
    interactive: false,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var heat = AA.clamp(AA.monitor("heat", 0), 0, 1);
        var lat = AA.dev.data.latencySamples;
        AA.spaced(g, "TUBE HEAT", r[0], r[1] + 5, 6.5, t.faint, t.bold, 0.6);
        AA.text(g, "LAT " + AA.number(lat / 48, 0) + " ms", r[0] + r[2], r[1] + 5, 7, t.faint, t.medium, "right");
        var by = r[1] + r[3] - 6;
        AA.fill(g, r[0], by, r[2], 4, 2, t.track);
        if (heat > 0.005) {
            var hw = Math.max(4, r[2] * heat);
            AA.fill(g, r[0], by - 2, hw, 8, 4, AA.alpha(t.warm, 0.18 * heat + 0.05));
            AA.fillGradient(g, r[0], by, hw, 4, 2, t.warm, AA.mix(t.warm, t.hot, heat), false);
        }
    }
});

// Browser preview only (scripts/build_m4l.py --preview): a sung phrase with vibrato and the mode's output.
AA.demo = function (sec) {
    var phrase = [57, 57, 60, 62, 64, 62, 60, -1, 55, 57, 59, 60, 60, -1];
    var idx = Math.floor(sec * 2.2) % phrase.length;
    var base = phrase[idx];
    if (base < 0) return { inhz: 0, outhz: 0, heat: 0, held: -1 };
    var within = (sec * 2.2) % 1;
    var inm = base + 0.25 * Math.sin(sec * 33) + (within < 0.15 ? (within - 0.15) * 3 : 0) + 0.12;
    var md = Parrot.mode();
    var p = Parrot.pitch();
    var outm = inm + p;
    if (md === 1) outm = Math.round(inm + p) + 0.04 * Math.sin(sec * 33);
    if (md === 2) outm = Parrot.robotNote();
    var hz = function (m) { return 440 * Math.pow(2, (m - 69) / 12); };
    var drive = (AA.values.drive || 0) / 100;
    return {
        inhz: hz(inm),
        outhz: hz(outm),
        heat: drive * (0.6 + 0.4 * Math.abs(Math.sin(sec * 5))),
        held: -1
    };
};
