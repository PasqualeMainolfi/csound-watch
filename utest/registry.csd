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
gicapacity_handles[] init 33

instr CreateGraph
    ihandle watchscope 0.5, 4, 4
    gihandles[p4] = ihandle
endin

instr VerifyRegistry
    assert_true(gihandles[0] == 1)
    assert_true(gihandles[1] == 2)
    assert_true(gihandles[2] == 3)
endin

instr CreateCapacityGraph
    ihandle watchscope 0.5
    gicapacity_handles[p4] = ihandle
endin

instr FillRegistry
    iindex = 0
    while iindex < 33 do
        schedule "CreateCapacityGraph", 0, 0.1, iindex
        iindex += 1
    od
    schedule "VerifyCapacity", 0.01, 0.01
endin

instr VerifyCapacity
    assert_true(gicapacity_handles[0] != 0)
    assert_true(gicapacity_handles[31] != 0)
    assert_true(gicapacity_handles[32] == 0)
endin
</CsInstruments>
<CsScore>
; The first graph is destroyed before the third is created. The third graph
; must reuse the empty slot without reusing handle 1 or overwriting graph 2.
i "CreateGraph"     0.00 0.10 0
i "CreateGraph"     0.00 0.50 1
i "CreateGraph"     0.20 0.10 2
i "VerifyRegistry"  0.21 0.01
i "FillRegistry"    0.60 0.01
e
</CsScore>
</CsoundSynthesizer>
