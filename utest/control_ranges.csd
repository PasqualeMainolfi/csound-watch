<CsoundSynthesizer>
<CsOptions>
-odac -m0 --run-unit-tests
</CsOptions>
<CsInstruments>
sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

; -----------------------------------------------------------------------------
; control_ranges.csd
;
; Control graphs whose y range does not straddle zero symmetrically. The
; vertical centre of a graph is the centre of its range, not the value 0: the
; zero line is drawn only when the range contains zero, and its position
; follows (0 - ymin) / (ymax - ymin).
;
; expseg is used on purpose: it cannot pass through zero, so it is the typical
; case of a signal living entirely in the positive half.
; -----------------------------------------------------------------------------

instr PositiveUnitRange
    ; range 0..1: zero line on the bottom edge, centre of the graph is 0.5
    graph:i = watchcontrol(2, 10, 8, 0, 1, "expseg, range 0..1 (zero at bottom)")
    assert_true(graph != 0)

    envelope:k = expseg(0.001, p3 * 0.5, 1, p3 * 0.5, 0.001)
    watchadd(graph, envelope)
endin

instr PositiveWideRange
    ; range 20..20000: zero is outside the range, so no zero line is drawn and
    ; the centre of the graph is 10010
    graph:i = watchcontrol(2, 10, 8, 20, 20000, "expseg 20..20000 Hz (no zero line)")
    assert_true(graph != 0)

    frequency:k = expseg(20, p3 * 0.5, 20000, p3 * 0.5, 20)
    watchadd(graph, frequency)
endin

instr AsymmetricRange
    ; range -0.25..1: zero line one fifth above the bottom edge
    graph:i = watchcontrol(2, 10, 8, -0.25, 1, "linseg, range -0.25..1")
    assert_true(graph != 0)

    value:k = linseg(-0.25, p3 * 0.5, 1, p3 * 0.5, -0.25)
    watchadd(graph, value)
endin

instr SymmetricRange
    ; reference: zero line at the centre
    graph:i = watchcontrol(2, 10, 8, -1, 1, "oscili, range -1..1 (zero centred)")
    assert_true(graph != 0)

    value:k = oscili(0.8, 1)
    watchadd(graph, value)
endin

instr DefaultRange
    ; no explicit limits: the viewer falls back to -1..1
    graph:i = watchcontrol(2)
    assert_true(graph != 0)

    value:k = oscili(0.8, 1)
    watchadd(graph, value)
endin

</CsInstruments>
<CsScore>
i "PositiveUnitRange" 0 10
i "PositiveWideRange" 0 10
i "AsymmetricRange"   0 10
i "SymmetricRange"    0 10
i "DefaultRange"      0 10
e
</CsScore>
</CsoundSynthesizer>
