<CsoundSynthesizer>
<CsOptions>
-o dac2
</CsOptions>
<CsInstruments>
sr = 44100
ksmps = 32
nchnls = 2
0dbfs = 1

spectrum@global:i = watchspectrum(0, 22050, -120, 0, 2, 8, 8, "Spectrum")
spectrogram@global:i = watchspectrogram(2, 0, 22050, -120, 0, 2, 8, 8, "Spectrogram")

instr SpectralGraphs
    ; spectrum:i = watchspectrum(0, 22050, -120, 0, 2, 8, 8, "Spectrum")

    sig:a = diskin2("/Users/pm/AcaHub/AudioSamples/vox.wav", 1, 0, 1)
    sig_anal:f = pvsanal(sig, 1024, 256, 1024, 1)
    watchadd(spectrum, sig_anal)
    watchadd(spectrogram, sig_anal)
endin

</CsInstruments>
<CsScore>
i "SpectralGraphs" 0 30
e
</CsScore>
</CsoundSynthesizer>
