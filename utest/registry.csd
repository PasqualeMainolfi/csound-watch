<CsoundSynthesizer>
<CsOptions>
-n -m0 --run-unit-tests
</CsOptions>
<CsInstruments>
sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

gihandles[] init 3

instr CreateGraph
    ihandle watchscope 0.5, 4, 4
    gihandles[p4] = ihandle
endin

instr VerifyRegistry
    assert_true(gihandles[0] == 1)
    assert_true(gihandles[1] == 2)
    assert_true(gihandles[2] == 3)
endin
</CsInstruments>
<CsScore>
; The first note ends before the third graph is created. Graph IDs remain
; distinct whether earlier graphs have been released or still await an ACK.
i "CreateGraph"     0.00 0.10 0
i "CreateGraph"     0.00 0.50 1
i "CreateGraph"     0.20 0.10 2
i "VerifyRegistry"  0.21 0.01
e
</CsScore>
</CsoundSynthesizer>
