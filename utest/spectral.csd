<CsoundSynthesizer>
<CsOptions>
-o dac2
</CsOptions>
<CsInstruments>
sr = 44100
ksmps = 32
nchnls = 2
0dbfs = 1

instr SpectralGraphs
    spectrum:i = watchspectrum()
    spectrogram:i = watchspectrogram(1)
    spectrum_full:i = watchspectrum(0, 22050, 0, 1, 0, 8, 8, "Spectrum")
    spectrogram_full:i = watchspectrogram(2, 0, 22050, -120, 0, 2, 8, 8, "Spectrogram")

    assert_true(ispectrum != 0)
    assert_true(ispectrogram != 0)
    assert_true(ispectrum_full != 0)
    assert_true(ispectrogram_full != 0)

    signal:a = oscili(0.2, 440)
    sig:f = pvsanal(asignal, 1024, 256, 1024, 1)
    watchadd(spectrum, signal)
    watchadd(spectrogram, sig)
    watchadd(spectrum_full, sig)
    watchadd(spectrogram_full, sig)
endin

</CsInstruments>
<CsScore>
i "SpectralGraphs" 0 30
e
</CsScore>
</CsoundSynthesizer>
