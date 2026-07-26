<CsoundSynthesizer>
<CsOptions>
-odac -m0 --run-unit-tests
</CsOptions>
<CsInstruments>
sr = 44100
ksmps = 64
nchnls = 2
0dbfs = 1

; -----------------------------------------------------------------------------
; point.csd
;
; Fixed-plane graphs. What each window must show is written in its title:
;
;   * the circle must be a circle, not an ellipse, at any window shape, and the
;     zero axes must cross at its centre;
;   * two points attached to the same plane must be drawn in different colors
;     and keep their own trail;
;   * on a plane whose ranges exclude zero no axis is drawn, and the centre of
;     the plane is the centre of the ranges.
;
; The trail is about 100 ms long whatever the control rate: at kr = 689 Hz it
; retains 69 points, published 12 at a time, once per viewer frame.
; -----------------------------------------------------------------------------

instr Circle
    graph:i = watchpoint(-1, 1, -1, 1, "circle, axes crossing at the centre")
    assert_true(graph != 0)

    phase:k = phasor(0.5)
    watchadd(graph, cos(phase * 6.283185307), sin(phase * 6.283185307))
endin

instr TwoPoints
    graph:i = watchpoint(-1, 1, -1, 1, "two points, two colors")
    assert_true(graph != 0)

    slow:k = phasor(0.25)
    fast:k = phasor(0.75)
    watchadd(graph, cos(slow * 6.283185307), sin(slow * 6.283185307))
    watchadd(graph, 0.5 * cos(fast * 6.283185307 * 2), 0.5 * sin(fast * 6.283185307 * 3))
endin

instr PositivePlane
    ; x 0..1 and y 20..20000: zero is outside both ranges, so no axis is drawn
    graph:i = watchpoint(0, 1, 20, 20000, "positive plane, no zero axis")
    assert_true(graph != 0)

    sweep:k = phasor(0.2)
    frequency:k = expseg(20, p3 * 0.5, 20000, p3 * 0.5, 20)
    watchadd(graph, sweep, frequency)
endin

instr OutOfRange
    ; the plane never rescales: the point leaves the window and comes back
    graph:i = watchpoint(-1, 1, -1, 1, "point leaving a fixed plane")
    assert_true(graph != 0)

    phase:k = phasor(0.3)
    watchadd(graph, 2 * cos(phase * 6.283185307), 2 * sin(phase * 6.283185307))
endin

</CsInstruments>
<CsScore>
i "Circle"        0 15
i "TwoPoints"     0 15
i "PositivePlane" 0 15
i "OutOfRange"    0 15
e
</CsScore>
</CsoundSynthesizer>
