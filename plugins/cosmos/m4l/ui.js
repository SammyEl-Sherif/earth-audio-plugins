// Cosmos: the mode family readout, the looper display (state, loop position ring, length) and the REC/PLAY/DUB
// button whose label follows the looper like the plugin's. ES5 (see aa_ui.js).

var Cosmos = {
    // Looper states (out3 of cosmos.genexpr).
    NAMES: ["EMPTY", "RECORDING", "PLAYING", "OVERDUB", "STOPPED"],
    TONES: [null, "hot", "good", "warm", null],
    // What REC/PLAY does next, per state: record, close the loop, overdub, stop overdubbing, restart.
    NEXT: ["REC", "PLAY", "DUB", "PLAY", "PLAY"],
    NEXT_TONES: ["hot", "good", "warm", "good", "good"]
};

Cosmos.state = function () {
    return AA.clamp(Math.round(AA.monitor("lstate", 0)), 0, 4);
};

Cosmos.tone = function (state) {
    var name = Cosmos.TONES[state];
    return name ? AA.tone(name) : AA.theme.faint;
};

AA.widget("cosmos.family", {
    interactive: false,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var mode = Math.round(AA.values[w.spec.param]);
        var family = AA.dev.data.families[AA.dev.data.familyOf[mode] || 0];
        AA.spaced(g, family, r[0] + r[2], r[1] + r[3] / 2 + 1, 7, t.accent, t.bold, 0.9, "right");
    }
});

AA.widget("cosmos.looper", {
    interactive: false,
    paint: function (g, w) {
        var t = AA.theme;
        var r = w.rect;
        var state = Cosmos.state();
        var tone = Cosmos.tone(state);
        var len = AA.monitor("llen", 0);
        // While recording the loop has no length yet: the head goes round once every 2 s recorded.
        var pos = state === 1 ? (len % 2) / 2 : AA.clamp(AA.monitor("lpos", 0), 0, 1);
        AA.fill(g, r[0], r[1], r[2], r[3], 6, t.well);
        AA.stroke(g, r[0], r[1], r[2], r[3], 6, t.line, 1);

        // Ring: the loop, with the play head running round it (filling while recording).
        var cx = r[0] + r[3] / 2 + 1;
        var cy = r[1] + r[3] / 2;
        var rad = r[3] / 2 - 7;
        var top = -Math.PI / 2;
        AA.drawArc(g, cx, cy, rad, 0, Math.PI * 2, t.track, null, 3);
        if (state >= 1 && state <= 3) {
            AA.drawArc(g, cx, cy, rad, top, top + Math.PI * 2, AA.alpha(tone, 0.25), null, 3);
            AA.drawArc(g, cx, cy, rad, top, top + Math.PI * 2 * Math.max(pos, 0.002), tone, null, 3);
        } else if (state === 4) {
            AA.drawArc(g, cx, cy, rad, top, top + Math.PI * 2, AA.alpha(t.dim, 0.35), null, 3);
        }
        if (state >= 1 && state <= 3) {
            var a = top + Math.PI * 2 * pos;
            AA.circle(g, cx + Math.cos(a) * rad, cy + Math.sin(a) * rad, 5, AA.alpha(tone, 0.25));
            AA.circle(g, cx + Math.cos(a) * rad, cy + Math.sin(a) * rad, 2.5, t.text);
        }

        // Centre glyph: dot (record), triangle (play), plus (dub), square (stopped).
        if (state === 1) {
            var pulse = 0.65 + 0.35 * Math.abs(Math.sin(AA.time * 0.12));
            AA.circle(g, cx, cy, 4.5, AA.alpha(tone, pulse));
        } else if (state === 2) {
            g.new_path();
            g.move_to(cx - 3, cy - 4.5);
            g.line_to(cx + 4.5, cy);
            g.line_to(cx - 3, cy + 4.5);
            g.close_path();
            AA.color(g, tone);
            g.fill();
        } else if (state === 3) {
            AA.line(g, cx - 4.5, cy, cx + 4.5, cy, tone, 2);
            AA.line(g, cx, cy - 4.5, cx, cy + 4.5, tone, 2);
        } else if (state === 4) {
            AA.fill(g, cx - 3.5, cy - 3.5, 7, 7, 1, t.dim);
        } else {
            AA.circle(g, cx, cy, 2, t.track);
        }

        // State and length.
        var tx = r[0] + r[3] + 4;
        AA.spaced(g, Cosmos.NAMES[state], tx, r[1] + 15, 7.5, state === 0 || state === 4 ? t.dim : tone, t.bold, 0.8);
        var lenText = len > 0.05 || state === 1 ? AA.number(len, 1) + " s" : "--";
        AA.text(g, lenText, tx, r[1] + 30, 12, state === 0 ? t.faint : t.text, t.bold, "left");
        var note = state === 0 ? "up to 60 s" : (AA.values.loopreverse > 0.5 ? "reversed" : "");
        if (note) AA.text(g, note, r[0] + r[2] - 7, r[1] + 30, 7.5, t.faint, t.medium, "right");
    },
    tick: function (w) {
        return Cosmos.state() === 1;  // keep the record dot pulsing
    }
});

// REC/PLAY/DUB: one button, labelled with what the next press does; lit (in the state's tone) while it records.
AA.widget("cosmos.rec", {
    hover: true,
    paint: function (g, w) {
        var state = Cosmos.state();
        var writing = state === 1 || state === 3;
        AA.pill(g, w, writing, w.flash || 0, Cosmos.NEXT[state], writing ? Cosmos.TONES[state] : Cosmos.NEXT_TONES[state]);
    },
    down: function (w) {
        AA.set(w.spec.param, 1);
        w.flash = 1;
        AA.animate(w);
    }
});

// Browser preview only (scripts/build_m4l.py --preview): record a 4 s loop, play, overdub, play, stop, clear.
AA.demo = function (sec) {
    var loop = 4;
    var t = sec % 24;
    var state = 0;
    var pos = 0;
    var len = 0;
    if (t < 0.6) {
        state = 0;
    } else if (t < 0.6 + loop) {
        state = 1;
        len = t - 0.6;
    } else if (t < 21) {
        len = loop;
        pos = ((t - 0.6 - loop) % loop) / loop;
        state = t < 12 ? 2 : (t < 16 ? 3 : (t < 19 ? 2 : 4));
        if (state === 4) pos = 0;
    }
    var level = 0.3 + 0.25 * Math.abs(Math.sin(sec * 2.3));
    return { lstate: state, lpos: pos, llen: len, peakl: level, peakr: level * 0.94 };
};
