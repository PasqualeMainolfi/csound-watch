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
; table_gens.csd
;
; Plot tables produced by different GEN routines and check how the viewer maps
; the y axis. The zero line is drawn only when the displayed range contains 0,
; and its height follows the range: it sits at the bottom edge for 0..1, one
; fifth above the bottom for -0.25..1, and at the centre only for -1..1.
; -----------------------------------------------------------------------------

; GEN5: exponential segments, strictly positive, never crosses zero
exponential@global:i = ftgen(0, 0, 1024, 5, 0.001, 512, 1, 512, 0.001)

; GEN7: straight segments, crosses zero twice
lines@global:i = ftgen(0, 0, 1024, 7, -1, 256, 1, 512, -0.5, 256, 0)

; GEN8: cubic spline through the same kind of breakpoints
spline@global:i = ftgen(0, 0, 1024, 8, 0, 256, 0.8, 512, -0.8, 256, 0)

; Automatic range: derived from the table itself, so 0 falls outside the
; displayed 0.001..1 interval and no zero line is drawn.
watchtable(exponential)

; Explicit 0..1: the zero line must sit exactly on the bottom edge.
watchtable(exponential, 0, 1, "GEN5 exponential, range 0..1")

; Automatic range over a table that crosses zero.
watchtable(lines)

; Explicit asymmetric range: the zero line must sit one fifth above the bottom.
watchtable(lines, -0.25, 1, "GEN7 segments, range -0.25..1")

; Symmetric reference: the zero line must sit at the centre.
watchtable(spline, -1, 1, "GEN8 spline, range -1..1")

instr VerifyTables
    assert_true(exponential != 0)
    assert_true(lines != 0)
    assert_true(spline != 0)

    ; GEN5 is strictly positive, GEN7 and GEN8 reach both signs
    assert_true(table(0, exponential) > 0)
    assert_true(table(512, exponential) == 1)
    assert_true(table(0, lines) == -1)
    assert_true(table(256, lines) == 1)
endin

</CsInstruments>
<CsScore>
i "VerifyTables" 0 10
e
</CsScore>
</CsoundSynthesizer>
