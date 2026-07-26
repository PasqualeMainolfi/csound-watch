<CsoundSynthesizer>
<CsOptions>
-odac -m0
</CsOptions>
<CsInstruments>

; -----------------------------------------------------------------------------
; watchpoint.csd
;
; Draw a Lissajous figure as a moving point on a fixed plane. The plot area is
; square whatever the window shape, so the figure keeps its proportions when the
; window is resized.
; -----------------------------------------------------------------------------

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

instr Lissajous
    graph:i = watchpoint(-1, 1, -1, 1, "Lissajous 3:2")

    phase:k = phasor(0.25)
    x:k = cos(phase * 6.283185307 * 3)
    y:k = sin(phase * 6.283185307 * 2)

    watchadd(graph, x, y)
endin

</CsInstruments>
<CsScore>
i "Lissajous" 0 20
</CsScore>
</CsoundSynthesizer>
