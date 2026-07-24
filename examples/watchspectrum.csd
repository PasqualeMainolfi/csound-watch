<CsoundSynthesizer>
<CsOptions>
-odac
</CsOptions>
<CsInstruments>

; -----------------------------------------------------------------------------
; watchspectrum.csd
;
; Analyze a harmonic signal with pvsanal and display its power spectrum in dB.
; -----------------------------------------------------------------------------

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

instr Spectrum
    graph:i = watchspectrum(20, 12000, -120, 0, 2, 10, 8, "Harmonic spectrum")

    signal:a = vco2(0.35, 220)
    analysis:f = pvsanal(signal, 2048, 512, 2048, 1)

    watchadd(graph, analysis)
    out(signal, signal)
endin

</CsInstruments>
<CsScore>
i "Spectrum" 0 20
</CsScore>
</CsoundSynthesizer>
