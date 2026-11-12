// Node smoke test for a device's jsui script: boots it against a recording stand-in for Max (no canvas), paints,
// then clicks, drags, double-clicks, scrolls and hovers across the whole device. Fails on any exception, on
// non-finite drawing coordinates, or on an outlet message the patcher wouldn't route.
//   node smoke.js <device ui script>      (run by shared/m4l/tests/test_aamax.py)
"use strict";
var fs = require("fs");
var vm = require("vm");

var source = fs.readFileSync(process.argv[2], "utf8");
var errors = [];
var sent = [];
var intervals = [];

function pattern() {
    return { add_color_stop_rgba: function () {} };
}
function check(name) {
    return function () {
        for (var i = 0; i < arguments.length; i++) {
            if (typeof arguments[i] === "number" && !isFinite(arguments[i])) errors.push(name + " got " + arguments[i]);
        }
    };
}
var mg = { relative_coords: 0, autofill: 0 };
["init", "redraw", "set_source_rgba", "set_source", "new_path", "close_path", "move_to", "line_to", "curve_to",
    "rectangle", "rectangle_rounded", "ellipse", "arc", "arc_negative", "fill", "fill_preserve", "stroke",
    "stroke_preserve", "set_line_width", "set_line_cap", "set_line_join", "select_font_face", "set_font_size",
    "show_text", "save", "restore", "translate", "rotate", "scale"].forEach(function (n) { mg[n] = check(n); });
mg.pattern_create_linear = pattern;
mg.pattern_create_radial = pattern;
mg.text_measure = function (s) { return [String(s).length * 5, 10]; };
mg.font_extents = function () { return [8, 2, 10]; };

var ctx = {
    mgraphics: mg,
    post: function (s) { errors.push("post: " + s); },
    Task: function (fn, that) {
        this.repeat = function () { intervals.push(function () { fn.call(that); }); };
    },
    outlet: function () {
        var args = Array.prototype.slice.call(arguments, 1);
        if (args.length === 1 && Array.isArray(args[0])) args = args[0];
        sent.push(args);
        var id = args[0];
        if (ctx.AA.params[id]) ctx.param.apply(null, args);
        else if (id !== "gen" && id !== "ext") errors.push("outlet: unroutable message " + args.join(" "));
    }
};
vm.createContext(ctx);
vm.runInContext(source, ctx, { filename: process.argv[2] });

var AA = ctx.AA;
var W = AA.dev.width;
var H = AA.dev.height;
function frame() {
    intervals.forEach(function (f) { f(); });
    ctx.paint();
}
function run(label, fn) {
    try {
        fn();
    } catch (e) {
        errors.push(label + ": " + (e.stack || e));
    }
}

run("first paint", frame);
for (var name in AA.params) {
    var s = AA.params[name];
    if (s.kind === "blob") run("restore " + name, function () { ctx.param.apply(null, [name].concat(s.def.length ? s.def : [0])); });
}
run("demo", function () { if (AA.demo) { var d = AA.demo(1.234); for (var k in d) ctx.mon.apply(null, [k].concat(d[k])); } });
frame();

var mods = [[0, 0, 0], [0, 1, 0], [0, 0, 1]]; // cmd, shift, option
for (var y = 2; y < H; y += 7) {
    for (var x = 2; x < W; x += 9) {
        run("hover " + x + "," + y, function () { ctx.onidle(x, y); });
        var m = mods[(x + y) % 3];
        run("click " + x + "," + y, function () {
            ctx.onclick(x, y, 1, m[0], m[1], 0, m[2], 0);
            ctx.ondrag(x + 3, y - 11, 1, m[0], m[1], 0, m[2], 0);
            ctx.ondrag(x + 3, y - 11, 0, m[0], m[1], 0, m[2], 0);
        });
        if ((x + y) % 4 === 0) run("dblclick " + x + "," + y, function () { ctx.ondblclick(x, y, 1, 0, 0, 0, 0, 0); });
        if ((x + y) % 5 === 0) run("wheel " + x + "," + y, function () { ctx.onwheel(x, y, 0, 1); });
    }
    run("paint row " + y, frame);
}
run("idle out", function () { ctx.onidleout(); frame(); });

if (errors.length) {
    console.error(errors.slice(0, 20).join("\n"));
    process.exit(1);
}
console.log("ok " + AA.dev.title + ": " + sent.length + " messages, " + AA.widgets.length + " widgets");
