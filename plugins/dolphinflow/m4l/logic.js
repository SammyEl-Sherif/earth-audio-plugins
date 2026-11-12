// Dolphin Flow for Max for Live: the chord logic, in a `js` object (ES5; Max's js runs old JavaScript).
// A port of Source/dsp/Chords.h, Source/dsp/VoiceLeader.h, the held-root stack of DolphinFlowProcessor and the
// voicing side of Source/dsp/Progression.h. gen~ (dolphinflow.genexpr) does the sound, the glide and the loop's
// timing; this only decides which note each voice goes to, at note events and parameter changes (never per sample).
//
// In (one inlet):  note <pitch> <velocity>      from midiparse (velocity 0 = note off)
//                  cc <number> <value>          123 all notes off, 120 all sound off
//                  chord | voices | octave | latch | loopon <v>, s<k>root | s<k>chord | s<k>len <v>
//                  active <0|1>, seqslot <k>    gen~'s envelope state and the loop's playing slot
//                  clip <n>                     the Write Clip button (a press counter)
//                  ready                        the device has loaded (live.thisdevice)
// Out 0 (gen~):    n0..n5 <note>, nv <count>, gon <0|1>, trig <n>  a voicing (trig last: gen~ latches on it)
//                  rel <n>, kill <n>                                release / silence (counters)
//                  t<k>len <beats>, t<k>n<j> <note>, lv <count>, tver <n>   the loop's settled voicings
// Out 1 (UI):      chord <seq> <root> <shape> <struck>, clip <seq> <status> <scene>   (the patch prepends "mon")

autowatch = 0;
inlets = 1;
outlets = 2;

// ---------------------------------------------------------------- chords (Source/dsp/Chords.h)
// device.py checks this table against the C++ at build time.
var SHAPES = [
    ["MAJ", "", [0, 4, 7]],
    ["MIN", "m", [0, 3, 7]],
    ["7", "7", [0, 4, 7, 10]],
    ["MAJ7", "maj7", [0, 4, 7, 11]],
    ["MIN7", "m7", [0, 3, 7, 10]],
    ["SUS2", "sus2", [0, 2, 7]],
    ["SUS4", "sus4", [0, 5, 7]],
    ["DIM", "dim", [0, 3, 6]],
    ["ADD9", "add9", [0, 4, 7, 14]],
    ["MIN9", "m9", [0, 3, 7, 10, 14]]
];
var NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
var SLOT_BEATS = [1, 2, 3, 4, 6, 8, 12, 16];
var NUM_SLOTS = 8;
var MIN_VOICES = 3;
var MAX_VOICES = 6;
var RANGE = 16; // notes stay within this many semitones of the register centre
var DEFAULT_CENTRE = 60;
var REFERENCE_OCTAVE = 5; // MIDI 60-71 plays at OCTAVE
var MAX_TOTAL_OCTAVES = 3;
var MIN_VOICED_NOTES = 3; // this many MIDI notes held = play exactly those notes
var MAX_HELD = 32;

function clampi(v, lo, hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

function pitchClass(n) {
    return ((n % 12) + 12) % 12;
}

function shapeOf(index) {
    return SHAPES[clampi(index, 0, SHAPES.length - 1)];
}

function chordName(root, shape) {
    if (root < 0) return "";
    return NAMES[pitchClass(root)] + (shape >= 0 ? shapeOf(shape)[1] : "");
}

// The chord shape whose pitch classes match exactly, preferring the lowest note as root. {root, shape}, shape -1
// when nothing matches (root = the lowest note's pitch class).
function identifyChord(notes) {
    var out = { root: 0, shape: -1 };
    if (!notes.length) return out;
    var played = 0;
    var lowest = notes[0];
    var i;
    for (i = 0; i < notes.length; i++) {
        played |= 1 << pitchClass(notes[i]);
        if (notes[i] < lowest) lowest = notes[i];
    }
    out.root = pitchClass(lowest);
    for (var attempt = 0; attempt < 12; attempt++) {
        var candidate = (out.root + attempt) % 12;
        if ((played & (1 << candidate)) === 0) continue;
        for (var s = 0; s < SHAPES.length; s++) {
            var tones = 0;
            var iv = SHAPES[s][2];
            for (i = 0; i < iv.length; i++) tones |= 1 << pitchClass(candidate + iv[i]);
            if (tones === played) {
                out.root = candidate;
                out.shape = s;
                return out;
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- voice leading (Source/dsp/VoiceLeader.h)

// Chord tones the voices must cover, most important first; with fewer voices than tones the fifth goes first,
// then the ninth.
function tonesFor(root, shape, voices) {
    var iv = shapeOf(shape)[2];
    var keepOrder = [0, 1, 3, 4, 2];
    var keep = Math.min(voices, iv.length);
    var set = { pcs: [], colour: [] };
    for (var k = 0; k < keepOrder.length && set.pcs.length < keep; k++) {
        var index = keepOrder[k];
        if (index >= iv.length) continue;
        set.pcs.push(pitchClass(root + iv[index]));
        set.colour.push(index !== 0 && index !== 2);
    }
    return set;
}

// The note of `pc` nearest `previous`, folded by octaves into [low, high].
function nearest(previous, pc, low, high) {
    var note = previous + ((pc - pitchClass(previous) + 18) % 12) - 6;
    while (note < low) note += 12;
    while (note > high) note -= 12;
    return note;
}

// Where each voice goes so the voices move as little as possible. Same search and costs as the C++ (every
// assignment of tones to voices that covers all kept tones, first cheapest wins), in exact integer units:
// costs x 20 x count, so 1 semitone = 20c, unison 3 = 60c, rootless bass 1.5 = 30c, doubled colour 0.75 = 15c,
// centre gravity 0.6 x |sum - count x centre| / count = 12 x |...|. The C++ sums floats; exact ties can differ.
function lead(from, root, shape, voices, centre) {
    var count = clampi(voices, MIN_VOICES, MAX_VOICES);
    if (!from || from.length !== count) return initial(root, shape, count, centre);
    var tones = tonesFor(root, shape, count);
    var nt = tones.pcs.length;
    var low = centre - RANGE;
    var high = centre + RANGE;
    var rootPc = pitchClass(root);
    var all = (1 << nt) - 1;
    var unit = 20 * count;
    var v, t;

    // Each voice's note and movement for each tone (the search only adds these up).
    var noteOf = [];
    var moveOf = [];
    for (v = 0; v < count; v++) {
        noteOf.push([]);
        moveOf.push([]);
        for (t = 0; t < nt; t++) {
            var n = nearest(from[v], tones.pcs[t], low, high);
            noteOf[v].push(n);
            moveOf[v].push(Math.abs(n - from[v]) * unit);
        }
    }

    var choice = [0, 0, 0, 0, 0, 0];
    var best = null;
    var bestCost = 1e18;
    var notes = [0, 0, 0, 0, 0, 0];
    for (;;) {
        var covered = 0;
        for (v = 0; v < count; v++) covered |= 1 << choice[v];
        if (covered === all) {
            var cost = 0;
            var sum = 0;
            var lowest = 0;
            var seen = [0, 0, 0, 0, 0];
            for (v = 0; v < count; v++) {
                var c = choice[v];
                var note = noteOf[v][c];
                notes[v] = note;
                cost += moveOf[v][c];
                sum += note;
                if (v === 0 || note < notes[lowest]) lowest = v;
                if (seen[c]++ > 0 && tones.colour[c]) cost += 15 * count;
            }
            if (cost < bestCost) {
                for (var a = 0; a < count; a++) {
                    for (var b = a + 1; b < count; b++) {
                        if (notes[a] === notes[b]) cost += 60 * count;
                    }
                }
                if (pitchClass(notes[lowest]) !== rootPc) cost += 30 * count;
                cost += 12 * Math.abs(sum - count * centre);
                if (cost < bestCost) {
                    bestCost = cost;
                    best = notes.slice(0, count);
                }
            }
        }
        var digit = 0;
        while (digit < count && ++choice[digit] === nt) choice[digit++] = 0;
        if (digit === count) break;
    }
    return best;
}

// First chord after silence: led from voices spread evenly around the centre (an open, centred voicing).
function initial(root, shape, voices, centre) {
    var count = clampi(voices, MIN_VOICES, MAX_VOICES);
    var spread = [];
    for (var i = 0; i < count; i++) spread.push(centre - 7 + Math.floor((14 * i) / (count - 1)));
    return lead(spread, root, shape, count, centre);
}

// ---------------------------------------------------------------- the arrangement (Source/dsp/Progression.h)

var slots = [];
(function () {
    // First open: Am7 - Fmaj7 - C - G, a bar each (DolphinFlowProcessor's defaultSlot).
    var roots = [9, 5, 0, 7];
    var shapes = [4, 3, 0, 0];
    for (var i = 0; i < NUM_SLOTS; i++) slots.push({ root: i < 4 ? roots[i] : -1, shape: i < 4 ? shapes[i] : 0, len: 3 });
})();

function loopBeats() {
    var total = 0;
    for (var i = 0; i < NUM_SLOTS; i++) if (slots[i].root >= 0) total += SLOT_BEATS[clampi(slots[i].len, 0, 7)];
    return total;
}

// The voicing each slot has once the loop has gone round (led through one pass to settle, then a second pass):
// what the loop plays from its first chord on, and what Write Clip writes. Empty slots get null.
function settledVoicings(voices, centre) {
    var order = [];
    var out = [];
    var i;
    for (i = 0; i < NUM_SLOTS; i++) {
        out.push(null);
        if (slots[i].root >= 0) order.push(i);
    }
    if (!order.length) return out;
    var v = initial(slots[order[0]].root, slots[order[0]].shape, voices, centre);
    for (i = 1; i < order.length; i++) v = lead(v, slots[order[i]].root, slots[order[i]].shape, voices, centre);
    for (i = 0; i < order.length; i++) {
        v = lead(v, slots[order[i]].root, slots[order[i]].shape, voices, centre);
        out[order[i]] = v;
    }
    return out;
}

// One pass of the loop as notes {note, start, end} in beats, common tones of consecutive chords tied (buildClip).
function buildClip(voices, centre) {
    var table = settledVoicings(voices, centre);
    var notes = [];
    var open = [];
    var time = 0;
    for (var i = 0; i < NUM_SLOTS; i++) {
        if (!table[i]) continue;
        var wanted = uniqueSorted(table[i]);
        var stillOpen = [];
        for (var k = 0; k < open.length; k++) {
            var n = notes[open[k]];
            var at = wanted.indexOf(n.note);
            if (at >= 0) {
                stillOpen.push(open[k]);
                wanted.splice(at, 1);
            } else {
                n.end = time;
            }
        }
        for (k = 0; k < wanted.length; k++) {
            stillOpen.push(notes.length);
            notes.push({ note: wanted[k], start: time, end: time });
        }
        open = stillOpen;
        time += SLOT_BEATS[clampi(slots[i].len, 0, 7)];
    }
    for (k = 0; k < open.length; k++) notes[open[k]].end = time;
    return notes;
}

function uniqueSorted(list) {
    var s = list.slice(0).sort(function (a, b) { return a - b; });
    var out = [];
    for (var i = 0; i < s.length; i++) if (i === 0 || s[i] !== s[i - 1]) out.push(s[i]);
    return out;
}

// ---------------------------------------------------------------- state

var P = { chord: 0, voices: 1, octave: 0, latch: 0, loopon: 0 };
var held = []; // {source, root, note}, oldest first
var intent = { kind: 0, root: 0, shape: 0, octave: 0, notes: [] }; // kind 0 none, 1 chord, 2 voicing
var playedOctave = 0;
var lastLatch = 0;
var looping = false;
var table = settledVoicings(4, DEFAULT_CENTRE);
var tableVer = 0;
var seqSlot = -1;

// What gen~ is playing (the C++ engine's state, mirrored).
var E = { voicing: [], root: -1, shape: 0, gate: false, hasChord: false, active: false, voices: 4,
    centre: DEFAULT_CENTRE, registerShift: 0 };
var counters = { trig: 0, rel: 0, kill: 0, chord: 0, clip: 0 };
var dirty = false; // a voicing to send
var struck = false; // ... with a gate-on
var lastClip = -1;
var lastClipMs = -1e9;

function send() {
    outlet(0, Array.prototype.slice.call(arguments));
}

function toUi() {
    outlet(1, Array.prototype.slice.call(arguments));
}

// ---------------------------------------------------------------- the engine side (DolphinFlowEngine)

function sounding() {
    return E.hasChord && (E.gate || E.active);
}

function leadFrom() {
    var from = [];
    for (var v = 0; v < E.voicing.length; v++) from.push(clampi(E.voicing[v] + E.registerShift, 0, 127));
    E.registerShift = 0;
    return from;
}

function applyVoicing(next) {
    E.voicing = next.slice(0);
    E.registerShift = 0;
    dirty = true;
}

function gateOn() {
    E.hasChord = true;
    E.gate = true;
    E.active = true;
    struck = true;
    dirty = true;
}

function playChord(root, shape) {
    E.root = pitchClass(root);
    E.shape = clampi(shape, 0, SHAPES.length - 1);
    var next = sounding() ? lead(leadFrom(), E.root, E.shape, E.voices, E.centre) :
        initial(E.root, E.shape, E.voices, E.centre);
    applyVoicing(next);
    gateOn();
}

// Exactly these notes (sorted), each sounding voice taking the new note of the same rank so voices never cross.
function playVoicing(notes) {
    var next = notes.slice(0, MAX_VOICES).sort(function (a, b) { return a - b; });
    var id = identifyChord(next);
    E.root = id.root;
    E.shape = id.shape;
    if (sounding() && E.voicing.length === next.length) {
        var order = [];
        var v;
        for (v = 0; v < next.length; v++) order.push(v);
        order.sort(function (a, b) { return E.voicing[a] - E.voicing[b] || a - b; });
        var ranked = [];
        for (v = 0; v < next.length; v++) ranked[order[v]] = next[v];
        next = ranked;
    }
    applyVoicing(next);
    gateOn();
}

function release() {
    E.gate = false;
    counters.rel++;
    send("rel", counters.rel);
}

// The register and voice count from the parameters; a held named chord is re-voiced when they change.
function pushSettings() {
    var voices = MIN_VOICES + clampi(Math.round(P.voices), 0, MAX_VOICES - MIN_VOICES);
    var centre = DEFAULT_CENTRE + 12 * clampi(Math.round(P.octave) + playedOctave, -MAX_TOTAL_OCTAVES, MAX_TOTAL_OCTAVES);
    var revoice = E.hasChord && E.gate && E.shape >= 0 && (voices !== E.voices || centre !== E.centre);
    E.registerShift += centre - E.centre;
    E.voices = voices;
    E.centre = centre;
    if (revoice && !looping) applyVoicing(lead(leadFrom(), E.root, E.shape, E.voices, E.centre));
}

// Sends the voicing once per event, so gen~ glides once even when several things changed together.
function flush() {
    if (!dirty) return;
    for (var v = 0; v < MAX_VOICES; v++) send("n" + v, v < E.voicing.length ? E.voicing[v] : 0);
    send("nv", E.voicing.length);
    send("gon", struck ? 1 : 0);
    counters.trig++;
    send("trig", counters.trig);
    counters.chord++;
    toUi("chord", counters.chord, E.root, E.shape, struck ? 1 : 0);
    dirty = false;
    struck = false;
}

// ---------------------------------------------------------------- held keys (DolphinFlowProcessor)

function press(source, root, note) {
    unpress(source);
    if (held.length === MAX_HELD) held.shift();
    held.push({ source: source, root: root, note: note });
}

function unpress(source) {
    for (var i = 0; i < held.length; i++) {
        if (held[i].source === source) {
            held.splice(i, 1);
            return;
        }
    }
}

function wantedIntent() {
    var next = { kind: 0, root: 0, shape: 0, octave: 0, notes: [] };
    if (!held.length) return next;
    var newest = held[held.length - 1];
    next.kind = 1;
    next.root = newest.root;
    next.shape = clampi(Math.round(P.chord), 0, SHAPES.length - 1);
    if (newest.note < 0) return next;
    next.octave = Math.floor(newest.note / 12) - REFERENCE_OCTAVE;
    // Three or more notes at once are a voiced chord: play it as is. Two stay roots (legato overlaps).
    var notes = [];
    for (var i = 0; i < held.length; i++) if (held[i].note >= 0) notes.push(held[i].note);
    notes = uniqueSorted(notes);
    if (notes.length < MIN_VOICED_NOTES) return next;
    next.kind = 2;
    next.notes = notes.slice(0, MAX_VOICES);
    return next;
}

function sameIntent(a, b) {
    if (a.kind !== b.kind || a.root !== b.root || a.shape !== b.shape || a.octave !== b.octave) return false;
    if (a.kind !== 2) return true;
    return a.notes.join(",") === b.notes.join(",");
}

function resolve() {
    var next = wantedIntent();
    if (sameIntent(next, intent)) return;
    intent = next;
    if (intent.kind === 0) {
        if (!P.latch) release();
    } else if (intent.kind === 1) {
        // The played root's octave moves the register (Live's Z / X, or simply playing higher), on top of OCTAVE.
        if (intent.octave !== playedOctave) {
            playedOctave = intent.octave;
            pushSettings();
        }
        playChord(intent.root, intent.shape);
    } else {
        playVoicing(intent.notes);
    }
}

// Notes that arrive together (a chord from a clip, Push's pads) are applied together, one glide: a resolve is
// scheduled 1 ms after the first of them, after the rest of the queue.
var resolveTask = null;
function scheduleResolve() {
    if (looping) return;
    if (!resolveTask) {
        resolveTask = new Task(function () {
            resolve();
            flush();
        }, this);
    }
    resolveTask.cancel();
    resolveTask.schedule(1);
}

// ---------------------------------------------------------------- the loop table for gen~

function runLoop() {
    return P.loopon > 0.5 && loopBeats() > 0;
}

function sendTable() {
    table = settledVoicings(E.voices, DEFAULT_CENTRE + 12 * clampi(Math.round(P.octave), -2, 2));
    for (var k = 0; k < NUM_SLOTS; k++) {
        var v = table[k];
        send("t" + (k + 1) + "len", v ? SLOT_BEATS[clampi(slots[k].len, 0, 7)] : 0);
        for (var j = 0; j < MAX_VOICES; j++) send("t" + (k + 1) + "n" + j, v && j < v.length ? v[j] : 0);
    }
    send("lv", E.voices);
    tableVer++;
    send("tver", tableVer);
    updateLoop(); // after gen~ has the table, so both agree on whether the loop runs
}

var tableTask = null;
function scheduleTable() {
    if (!tableTask) tableTask = new Task(sendTable, this);
    tableTask.cancel();
    tableTask.schedule(15); // a set load or a Push bank sweep sends many slot values at once
}

// LOOP on or off (or the arrangement emptied / filled while LOOP is on).
function updateLoop() {
    var want = runLoop();
    if (want === looping) return;
    looping = want;
    if (looping) {
        if (resolveTask) resolveTask.cancel();
        seqSlot = -1;
        return;
    }
    // The loop owned the voices: lead on from what it was playing, release, then play any held keys.
    if (seqSlot >= 0 && table[seqSlot]) {
        E.voicing = table[seqSlot].slice(0);
        E.root = slots[seqSlot].root;
        E.shape = slots[seqSlot].shape;
        E.hasChord = true;
        E.registerShift = 0;
    }
    intent = { kind: 0, root: 0, shape: 0, octave: 0, notes: [] };
    release();
    resolve();
    flush();
}

// ---------------------------------------------------------------- messages

function note(pitch, velocity) {
    pitch = Math.round(pitch);
    if (velocity > 0) press(pitch, pitchClass(pitch), pitch);
    else unpress(pitch);
    scheduleResolve();
}

function cc(number, value) {
    if (number === 123 || number === 120) {
        held = [];
        intent = { kind: 0, root: 0, shape: 0, octave: 0, notes: [] };
        if (looping) return;
        if (number === 120) {
            E.gate = false;
            E.hasChord = false;
            counters.kill++;
            send("kill", counters.kill);
        } else {
            release();
        }
    }
}

function chord(v) {
    P.chord = v;
    if (looping) return;
    // A new chord quality re-voices a latched chord too.
    if (intent.kind === 0 && E.gate && E.root >= 0) playChord(E.root, clampi(Math.round(v), 0, SHAPES.length - 1));
    else resolve();
    flush();
}

function voices(v) {
    P.voices = v;
    pushSettings();
    flush();
    scheduleTable();
}

function octave(v) {
    P.octave = v;
    pushSettings();
    flush();
    scheduleTable();
}

function latch(v) {
    P.latch = v > 0.5 ? 1 : 0;
    // Switching LATCH off lets a latched chord go.
    if (!looping && lastLatch && !P.latch && !held.length) release();
    lastLatch = P.latch;
}

function loopon(v) {
    P.loopon = v;
    updateLoop();
}

function setSlot(k, field, v) {
    if (k < 0 || k >= NUM_SLOTS) return;
    slots[k][field] = Math.round(v);
    scheduleTable();
}

function active(v) {
    E.active = v > 0.5;
}

function seqslot(v) {
    if (v >= 0) seqSlot = Math.round(v); // kept when the loop stops, so the keys lead on from its last chord
}

function clip(n) {
    var now = new Date().getTime();
    if (n === lastClip || now - lastClipMs < 300) return; // one write per press
    lastClip = n;
    lastClipMs = now;
    writeClip();
}

function ready() {
    sendTable();
}

function anything() {
    // s<k>root (0 = empty, 1..12 = C..B) / s<k>chord / s<k>len <v>
    var m = /^s(\d)(root|chord|len)$/.exec(messagename);
    if (!m) return;
    var v = Math.round(arguments[0]);
    setSlot(parseInt(m[1], 10) - 1, m[2] === "chord" ? "shape" : m[2], m[2] === "root" ? v - 1 : v);
}

// ---------------------------------------------------------------- Write Clip (Live API)

function clipStatus(status, scene) {
    counters.clip++;
    toUi("clip", counters.clip, status, scene);
}

// Writes one pass of the loop, voiced as it plays, into the first empty clip slot of this device's track.
// UI status: 1 written (scene), 2 no empty slot, 3 empty arrangement, 4 Live API error.
function writeClip() {
    var notes = buildClip(E.voices, DEFAULT_CENTRE + 12 * clampi(Math.round(P.octave), -2, 2));
    if (!notes.length) {
        clipStatus(3, 0);
        return;
    }
    try {
        var path = "this_device";
        var track = null;
        for (var up = 0; up < 8; up++) {
            path += " canonical_parent";
            var obj = new LiveAPI(path);
            if (!obj || obj.id == 0) break;
            if (obj.type === "Track") {
                track = obj;
                break;
            }
        }
        if (!track) {
            clipStatus(4, 0);
            return;
        }
        var count = track.getcount("clip_slots");
        for (var i = 0; i < count; i++) {
            var slotPath = path + " clip_slots " + i;
            var slot = new LiveAPI(slotPath);
            if (Number(slot.get("has_clip")) !== 0) continue;
            slot.call("create_clip", loopBeats());
            var c = new LiveAPI(slotPath + " clip");
            c.call("set_notes");
            c.call("notes", notes.length);
            for (var k = 0; k < notes.length; k++) {
                var n = notes[k];
                c.call("note", n.note, Math.round(n.start * 10000) / 10000, Math.round((n.end - n.start) * 10000) / 10000, 100, 0);
            }
            c.call("done");
            var names = [];
            for (var s = 0; s < NUM_SLOTS; s++) if (slots[s].root >= 0) names.push(chordName(slots[s].root, slots[s].shape));
            c.set("name", "Dolphin Flow - " + names.join(" "));
            clipStatus(1, i + 1);
            return;
        }
        clipStatus(2, 0);
    } catch (e) {
        post("Dolphin Flow: Write Clip failed: " + e + "\n");
        clipStatus(4, 0);
    }
}
