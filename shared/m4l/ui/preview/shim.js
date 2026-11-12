// Runs a device's jsui script in a browser: a canvas-backed stand-in for Max's mgraphics, Task, outlet and
// post, with the patcher's param echo. Used by `scripts/build_m4l.py --preview` and the Node smoke test.
// Not shipped in devices.
(function (root) {
    function rgba(r, g, b, a) {
        return "rgba(" + Math.round(r * 255) + "," + Math.round(g * 255) + "," + Math.round(b * 255) + "," + a + ")";
    }

    function cssFont(face, size) {
        var weight = /Bold/.test(face) ? 700 : (/Light/.test(face) ? 300 : 500);
        return weight + " " + size + 'px "Ableton Sans", Inter, -apple-system, "Helvetica Neue", Arial, sans-serif';
    }

    root.makeMgraphics = function (ctx, onRedraw) {
        var style = "#000";
        var face = "Ableton Sans Medium";
        var size = 10;
        var pt = [0, 0];
        function font() {
            ctx.font = cssFont(face, size);
        }
        return {
            relative_coords: 0,
            autofill: 0,
            size: [0, 0],
            init: function () {},
            redraw: function () {
                onRedraw();
            },
            set_source_rgba: function (r, g, b, a) {
                if (r && r.length) {
                    a = r[3];
                    b = r[2];
                    g = r[1];
                    r = r[0];
                }
                style = rgba(r, g, b, a === undefined ? 1 : a);
            },
            set_source: function (p) {
                style = p.grad;
            },
            pattern_create_linear: function (x0, y0, x1, y1) {
                var grad = ctx.createLinearGradient(x0, y0, x1, y1);
                return { grad: grad, add_color_stop_rgba: function (o, r, g, b, a) { grad.addColorStop(o, rgba(r, g, b, a)); } };
            },
            pattern_create_radial: function (x0, y0, r0, x1, y1, r1) {
                var grad = ctx.createRadialGradient(x0, y0, r0, x1, y1, r1);
                return { grad: grad, add_color_stop_rgba: function (o, r, g, b, a) { grad.addColorStop(o, rgba(r, g, b, a)); } };
            },
            new_path: function () { ctx.beginPath(); },
            close_path: function () { ctx.closePath(); },
            move_to: function (x, y) { pt = [x, y]; ctx.moveTo(x, y); },
            line_to: function (x, y) { pt = [x, y]; ctx.lineTo(x, y); },
            curve_to: function (a, b, c, d, e, f) { pt = [e, f]; ctx.bezierCurveTo(a, b, c, d, e, f); },
            rectangle: function (x, y, w, h) { ctx.rect(x, y, w, h); },
            rectangle_rounded: function (x, y, w, h, ow, oh) {
                ctx.moveTo(x + w, y + h / 2);
                ctx.roundRect(x, y, w, h, Math.max(0, Math.min(ow / 2, w / 2, h / 2)));
            },
            ellipse: function (x, y, w, h) {
                ctx.moveTo(x + w, y + h / 2);
                ctx.ellipse(x + w / 2, y + h / 2, Math.abs(w / 2), Math.abs(h / 2), 0, 0, Math.PI * 2);
            },
            arc: function (x, y, r, a0, a1) { ctx.arc(x, y, r, a0, a1); },
            arc_negative: function (x, y, r, a0, a1) { ctx.arc(x, y, r, a0, a1, true); },
            fill: function () { ctx.fillStyle = style; ctx.fill(); ctx.beginPath(); },
            fill_preserve: function () { ctx.fillStyle = style; ctx.fill(); },
            stroke: function () { ctx.strokeStyle = style; ctx.stroke(); ctx.beginPath(); },
            stroke_preserve: function () { ctx.strokeStyle = style; ctx.stroke(); },
            set_line_width: function (w) { ctx.lineWidth = w; },
            set_line_cap: function (c) { ctx.lineCap = c; },
            set_line_join: function (j) { ctx.lineJoin = j; },
            select_font_face: function (f) { face = f; font(); },
            set_font_size: function (s) { size = s; font(); },
            text_measure: function (s) { font(); return [ctx.measureText(String(s)).width, size]; },
            font_extents: function () { return [size * 0.74, size * 0.2, size * 1.2]; },
            show_text: function (s) { font(); ctx.fillStyle = style; ctx.fillText(String(s), pt[0], pt[1]); },
            save: function () { ctx.save(); },
            restore: function () { ctx.restore(); },
            translate: function (x, y) { ctx.translate(x, y); },
            rotate: function (a) { ctx.rotate(a); },
            scale: function (x, y) { ctx.scale(x, y); }
        };
    };

    // Boots one device script into a fresh global scope (an iframe or a Node vm context).
    root.mountDevice = function (scope, canvas, source, log) {
        var ctx = canvas.getContext("2d");
        var pending = false;
        var g;
        function draw() {
            pending = false;
            ctx.setTransform(scope.__zoom, 0, 0, scope.__zoom, 0, 0);
            ctx.beginPath();
            scope.paint();
        }
        g = root.makeMgraphics(ctx, function () {
            if (!pending) {
                pending = true;
                scope.requestAnimationFrame(draw);
            }
        });
        scope.mgraphics = g;
        scope.post = function (s) { log && log("post: " + s); };
        scope.Task = function (fn, that) {
            var self = this;
            this.interval = 33;
            this.repeat = function () { self.id = scope.setInterval(function () { fn.call(that); }, self.interval); };
            this.cancel = function () { scope.clearInterval(self.id); };
        };
        scope.outlet = function () {
            var args = Array.prototype.slice.call(arguments, 1);
            if (args.length === 1 && args[0] && args[0].length !== undefined && typeof args[0] !== "string") args = args[0];
            log && log(args.join(" "));
            var id = args[0];
            if (scope.AA.params[id]) scope.param.apply(null, args);  // the patcher echoes parameter changes
            if (id === "ext" && scope.AA.preview && scope.AA.preview.ext) scope.AA.preview.ext(args.slice(1));
        };
        scope.eval(source);
        return draw;
    };
})(typeof window !== "undefined" ? window : this);
