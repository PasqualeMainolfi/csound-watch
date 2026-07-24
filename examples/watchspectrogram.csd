<CsoundSynthesizer>
<CsOptions>
-odac
</CsOptions>
<CsInstruments>

; -----------------------------------------------------------------------------
; watchspectrogram.csd
;
; Display four seconds of spectral history for a slowly swept oscillator.
; -----------------------------------------------------------------------------

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

instr Spectrogram
    graph:i = watchspectrogram(4, 20, 12000, -120, 0, 2, 8, 8, "Frequency sweep")

    frequency:k = expon(100, p3, 6000)
    signal:a = vco2(0.3, frequency)
    analysis:f = pvsanal(signal, 2048, 256, 2048, 1)

    watchadd(graph, analysis)
    out(signal, signal)
endin

</CsInstruments>
<CsScore>
i "Spectrogram" 0 20
</CsScore>
</CsoundSynthesizer>
