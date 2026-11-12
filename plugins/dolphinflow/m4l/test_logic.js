// Node tests for logic.js (the chord logic of the Max for Live device), ported from plugins/dolphinflow/tests:
//   node plugins/dolphinflow/m4l/test_logic.js
// Runs logic.js against a stand-in for Max's js (outlet, Task, post) and checks chords, voice leading, the held-key
// stack, the loop table and the clip, plus what reaches gen~.
"use strict";
var fs = require("fs");
var path = require("path");
var vm = require("vm");
var assert = require("assert");
var same = function (a, b) { assert.strictEqual(JSON.stringify(a), JSON.stringify(b)); }; // arrays cross vm realms

function load() {
    var sent = [];
    var ui = [];
    var tasks = [];
    var ctx = {
        post: function () {},
        outlet: function (n, list) {
            (n === 0 ? sent : ui).push(list);
        },
        Task: function (fn) {
            var task = { fn: fn, pending: false };
            this.schedule = function () { task.pending = true; };
            this.cancel = function () { task.pending = false; };
            tasks.push(task);
        }
    };
    vm.createContext(ctx);
    vm.runInContext(fs.readFileSync(path.join(__dirname, "logic.js"), "utf8"), ctx);
    ctx.sent = sent;
    ctx.ui = ui;
    ctx.run = function () { // fire due tasks, like Max's scheduler
        tasks.forEach(function (t) {
            if (t.pending) {
                t.pending = false;
                t.fn();
            }
        });
    };
    ctx.last = function (name) {
        for (var i = sent.length - 1; i >= 0; i--) if (sent[i][0] === name) return sent[i][1];
        return undefined;
    };
    ctx.voiced = function () {
        var out = [];
        for (var v = 0; v < ctx.last("nv"); v++) out.push(ctx.last("n" + v));
        return out;
    };
    return ctx;
}

var tests = [];
function test(name, fn) { tests.push([name, fn]); }

function pcs(v) { return v.map(function (n) { return ((n % 12) + 12) % 12; }); }
function moved(a, b) { var s = 0; for (var i = 0; i < a.length; i++) s += Math.abs(a[i] - b[i]); return s; }
var C = 0, D = 2, F = 5, G = 7, A = 9;

test("C to Am moves one voice up a whole tone", function () {
    var L = load();
    var am = L.lead([60, 64, 67, 72], A, 1, 4, 64);
    same(am, [60, 64, 69, 72]);
});

test("C to F holds the common tone", function () {
    var L = load();
    var f = L.lead([60, 64, 67, 72], F, 0, 4, 64);
    assert.strictEqual(f[0], 60);
    assert.strictEqual(f[3], 72);
    assert.ok(moved([60, 64, 67, 72], f) <= 3);
});

test("G7 resolves to C by small steps", function () {
    var L = load();
    var g7 = L.initial(G, 2, 4, 60);
    var c = L.lead(g7, C, 0, 4, 60);
    for (var i = 0; i < 4; i++) assert.ok(Math.abs(c[i] - g7[i]) <= 4);
    assert.ok(moved(g7, c) <= 7);
});

test("every chord tone sounds; thin voicings drop the fifth, then the ninth", function () {
    var L = load();
    [3, 4, 5, 6].forEach(function (voices) {
        for (var shape = 0; shape < 10; shape++) {
            [C, 4, A].forEach(function (root) {
                var chord = L.initial(root, shape, voices, 60);
                assert.strictEqual(chord.length, voices);
                var iv = L.SHAPES[shape][2];
                for (var i = 0; i < iv.length; i++) {
                    var dropped = voices < iv.length && (i === 2 || (voices === 3 && i === 4));
                    if (!dropped) assert.ok(pcs(chord).indexOf((root + iv[i]) % 12) >= 0);
                }
            });
        }
    });
    var dm9 = pcs(L.initial(D, 9, 3, 60));
    assert.ok(dm9.indexOf(D) >= 0 && dm9.indexOf(5) >= 0 && dm9.indexOf(0) >= 0);
});

test("chords are named from their notes", function () {
    var L = load();
    var id = L.identifyChord([57, 60, 64, 67]);
    assert.strictEqual(id.root, A);
    assert.strictEqual(id.shape, 4);
    id = L.identifyChord([64, 67, 72]);
    assert.strictEqual(id.root, C);
    assert.strictEqual(id.shape, 0);
    assert.strictEqual(L.identifyChord([60, 61, 62]).shape, -1);
});

test("MIDI notes pick the root, CHORD the quality; releasing falls back to the key still held", function () {
    var L = load();
    L.chord(4);
    L.note(57, 100);
    L.run();
    assert.strictEqual(L.E.root, A);
    assert.strictEqual(L.E.shape, 4);
    assert.ok(L.E.gate);
    assert.strictEqual(L.last("gon"), 1);
    L.note(64, 100);
    L.run();
    assert.strictEqual(L.E.root, 4);
    L.note(64, 0);
    L.run();
    assert.strictEqual(L.E.root, A);
    var rel = L.last("rel");
    L.note(57, 0);
    L.run();
    assert.ok(!L.E.gate);
    assert.strictEqual(L.last("rel"), (rel || 0) + 1);
});

test("notes arriving together are one chord (one trig)", function () {
    var L = load();
    L.note(62, 100);
    L.note(65, 100);
    L.note(69, 100);
    L.note(72, 100);
    L.run();
    var trigs = L.sent.filter(function (m) { return m[0] === "trig"; }).length;
    assert.strictEqual(trigs, 1);
    same(L.voiced(), [62, 65, 69, 72]); // three or more notes: played as they are
    assert.strictEqual(L.E.root, D);
    assert.strictEqual(L.E.shape, 4);
});

test("two overlapping roots stay roots", function () {
    var L = load();
    L.note(48, 100);
    L.run();
    L.note(53, 100);
    L.run();
    assert.strictEqual(L.E.root, F);
    assert.strictEqual(L.voiced().length, 4);
});

test("the played octave moves the register, like Live's Z / X", function () {
    var L = load();
    function mean() { var v = L.voiced(); return v.reduce(function (a, b) { return a + b; }, 0) / v.length; }
    L.note(60, 100);
    L.run();
    var home = mean();
    assert.ok(Math.abs(home - 60) < 6);
    L.note(60, 0);
    L.note(72, 100);
    L.run();
    assert.ok(Math.abs(mean() - (home + 12)) <= 2);
    L.note(72, 0);
    L.note(48, 100);
    L.run();
    assert.ok(Math.abs(mean() - (home - 12)) <= 2);
    L.octave(0); // OCTAVE -2 (choice index 0) on top: three octaves down at most
    L.note(48, 0);
    L.note(24, 100);
    L.run();
    assert.ok(Math.abs(mean() - (home - 36)) <= 2);
});

test("changing CHORD re-voices a held chord; LATCH holds after release and lets go when switched off", function () {
    var L = load();
    L.note(60, 100);
    L.run();
    L.chord(1);
    assert.strictEqual(L.E.shape, 1);
    L.latch(1);
    L.note(60, 0);
    L.run();
    assert.ok(L.E.gate);
    L.chord(4); // a latched chord follows CHORD too
    assert.strictEqual(L.E.shape, 4);
    assert.ok(L.E.gate);
    L.latch(0);
    assert.ok(!L.E.gate);
});

test("VOICES re-voices a held chord without a strike", function () {
    var L = load();
    L.note(60, 100);
    L.run();
    L.voices(3); // 6 voices
    assert.strictEqual(L.voiced().length, 6);
    assert.strictEqual(L.last("gon"), 0);
});

test("the loop table is the settled pass, and the clip ties common tones", function () {
    var L = load();
    L.ready();
    assert.strictEqual(L.last("t1len"), 4);
    assert.strictEqual(L.last("t5len"), 0);
    same([0, 1, 2, 3].map(function (j) { return L.last("t1n" + j); }), [52, 55, 60, 69]);
    var clip = L.buildClip(4, 60);
    var end = Math.max.apply(null, clip.map(function (n) { return n.end; }));
    assert.strictEqual(end, 16);
    assert.ok(clip.length < 16);
    var first = clip.filter(function (n) { return n.start === 0; }).map(function (n) { return n.note; }).sort();
    same(first, [52, 55, 60, 69]);
});

test("slot edits update the table; LOOP owns the voices, and stopping it plays held keys", function () {
    var L = load();
    L.ready();
    L.loopon(1);
    assert.ok(L.looping);
    var trigs = L.sent.length;
    L.note(62, 100);
    L.run();
    assert.strictEqual(L.sent.length, trigs); // keys tracked, nothing sent while the loop plays
    L.seqslot(2);
    L.loopon(0);
    assert.ok(!L.looping);
    assert.strictEqual(L.E.root, D); // the held key plays, led from the loop's chord
    assert.strictEqual(L.last("gon"), 1);
    // Emptying every slot while LOOP is on stops the loop too.
    L.loopon(1);
    for (var k = 1; k <= 8; k++) vm.runInContext("messagename = 's" + k + "root'; anything(0);", L);
    L.run();
    assert.strictEqual(L.last("t1len"), 0);
    assert.ok(!L.looping);
});

var failed = 0;
tests.forEach(function (t) {
    try {
        t[1]();
        console.log("ok   " + t[0]);
    } catch (e) {
        failed++;
        console.log("FAIL " + t[0] + "\n     " + (e.stack || e).split("\n").slice(0, 3).join("\n     "));
    }
});
console.log(tests.length - failed + "/" + tests.length + " passed");
process.exit(failed ? 1 : 0);
