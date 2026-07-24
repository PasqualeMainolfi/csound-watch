<CsoundSynthesizer>
<CsOptions>
-odac
</CsOptions>
<CsInstruments>

; -----------------------------------------------------------------------------
; watchscope.csd
;
; Create one oscilloscope window and attach two synchronized audio signals.
; The viewer is launched automatically and exits shortly after Csound stops.
; -----------------------------------------------------------------------------

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

instr Scope
    graph:i = watchscope(1, 10, 8, -1, 1, "Two oscillators")
    watchtheme(graph, 1)

    first:a = oscili(0.7, 2)
    second:a = oscili(0.3, 5)

    watchadd(graph, first)
    watchadd(graph, second)

    out(first + second, first + second)
endin

</CsInstruments>
<CsScore>
i "Scope" 0 20
</CsScore>
</CsoundSynthesizer>
