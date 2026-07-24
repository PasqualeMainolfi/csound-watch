<CsoundSynthesizer>
<CsOptions>
-o dac2
</CsOptions>
<CsInstruments>
sr = 44100
ksmps = 1
nchnls = 2
0dbfs = 1

instr ScopeStream
    sig:a = diskin2("/Users/pm/AcaHub/AudioSamples/vox.wav", 1, 0, 1)
    scope:i = watchscope(0.5, 30, 30, -1, 1, "Scope receiver test")
    scope_sampled:i = watchscope(1.0, 30, 30, -1, 1, "Scope receiver sampled test")
    ; assert_true(iscope != 0)

    signal1:a = oscili(0.9, 2)
    signal2:a = oscili(0.5, 5)
    watchadd(scope_sampled, sig)
    watchadd(scope, signal1)
    watchadd(scope, signal2)
endin

</CsInstruments>
<CsScore>
i "ScopeStream" 0 30
e
</CsScore>
</CsoundSynthesizer>
