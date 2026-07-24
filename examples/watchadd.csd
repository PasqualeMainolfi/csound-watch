<CsoundSynthesizer>
<CsOptions>
-odac
</CsOptions>
<CsInstruments>

; -----------------------------------------------------------------------------
; watchadd.csd
;
; Attach one audio signal to an oscilloscope and its PVS representation to both
; a spectrum and a spectrogram.
; -----------------------------------------------------------------------------

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

instr WatchSignals
    scope:i = watchscope(0.5, 10, 8, -1, 1, "Waveform")
    spectrum:i = watchspectrum(20, 12000, -120, 0, 2, 10, 8, "Spectrum")
    spectrogram:i = watchspectrogram(3, 20, 12000, -120, 0, 2, 8, 8, "Spectrogram")

    signal:a = vco2(0.35, 220)
    analysis:f = pvsanal(signal, 2048, 256, 2048, 1)

    watchadd(scope, signal)
    watchadd(spectrum, analysis)
    watchadd(spectrogram, analysis)

    out(signal, signal)
endin

</CsInstruments>
<CsScore>
i "WatchSignals" 0 20
</CsScore>
</CsoundSynthesizer>
