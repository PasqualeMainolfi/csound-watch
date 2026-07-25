<CsoundSynthesizer>
<CsOptions>
-odac
</CsOptions>
<CsInstruments>

; -----------------------------------------------------------------------------
; watchadd.csd
;
; Attach audio, control, and PVS signals to compatible graph domains.
; -----------------------------------------------------------------------------

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

instr WatchSignals
    scope:i = watchscope(0.5, 10, 8, -1, 1, "Waveform")
    control:i = watchcontrol(2, 10, 8, -1, 1, "Control signal")
    spectrum:i = watchspectrum(20, 12000, -120, 0, 2, 10, 8, "Spectrum")
    spectrogram:i = watchspectrogram(3, 20, 12000, -120, 0, 2, 8, 8, "Spectrogram")

    signal:a = vco2(0.35, 220)
    modulation:k = oscili(0.8, 1)
    analysis:f = pvsanal(signal, 2048, 256, 2048, 1)

    watchadd(scope, signal)
    watchadd(control, modulation)
    watchadd(spectrum, analysis)
    watchadd(spectrogram, analysis)

    out(signal, signal)
endin

</CsInstruments>
<CsScore>
i "WatchSignals" 0 20
</CsScore>
</CsoundSynthesizer>
