<CsoundSynthesizer>
<CsOptions>
-odac -m0
</CsOptions>
<CsInstruments>

; -----------------------------------------------------------------------------
; watchcontrol.csd
;
; Display two k-rate signals on a shared control-sample timeline.
; -----------------------------------------------------------------------------

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

instr ControlSignals
    graph:i = watchcontrol(2, 10, 8, -1, 1, "Control signals")

    sine:k = oscili(0.8, 1)
    phase:k = phasor(0.25)
    ramp:k = phase * 2 - 1

    watchadd(graph, sine)
    watchadd(graph, ramp)
endin

</CsInstruments>
<CsScore>
i "ControlSignals" 0 20
</CsScore>
</CsoundSynthesizer>
