<CsoundSynthesizer>
<CsOptions>
-n -m0 --run-unit-tests
</CsOptions>
<CsInstruments>
sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

gihandles[] init 33

instr CreateGraph
    ihandle watchscope 0.5
    gihandles[p4] = ihandle
endin

instr FillRegistry
    iindex = 0
    while iindex < 33 do
        schedule "CreateGraph", 0, 0.1, iindex
        iindex += 1
    od
    schedule "VerifyCapacity", 0.01, 0.01
endin

instr VerifyCapacity
    assert_true(gihandles[0] != 0)
    assert_true(gihandles[31] != 0)
    assert_true(gihandles[32] == 0)
endin
</CsInstruments>
<CsScore>
; A fresh Csound instance starts with an empty 32-slot registry.
i "FillRegistry" 0 0.01
e
</CsScore>
</CsoundSynthesizer>
