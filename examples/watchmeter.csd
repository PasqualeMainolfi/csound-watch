<CsoundSynthesizer>
<CsOptions>
-odac -m0
</CsOptions>
<CsInstruments>

; -----------------------------------------------------------------------------
; watchmeter.csd
;
; Four voices metered in decibels. The levels are measured in the orchestra with
; rms and converted with dbfsamp: watchmeter plots what it is given, so the unit
; of the array and the scale argument have to agree.
;
; watchmeter owns its window, so there is no watchadd here: the length of the
; array declares the four bars.
; -----------------------------------------------------------------------------

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

instr Quartet
    bass:a = vco2(0.30, 55) * oscili(1, 0.13)
    tenor:a = vco2(0.22, 165) * oscili(1, 0.21)
    alto:a = vco2(0.16, 330) * oscili(1, 0.34)
    soprano:a = vco2(0.10, 660) * oscili(1, 0.55)

    ; a small offset keeps the logarithm finite while the voice is silent
    levels:k[] init 4
    levels[0] = dbfsamp(rms(bass) + 0.000001)
    levels[1] = dbfsamp(rms(tenor) + 0.000001)
    levels[2] = dbfsamp(rms(alto) + 0.000001)
    levels[3] = dbfsamp(rms(soprano) + 0.000001)

    graph:i = watchmeter(levels, -60, 0, 2, "Quartet levels (dB)")

    mix:a = (bass + tenor + alto + soprano) * 0.5
    out(mix, mix)
endin

</CsInstruments>
<CsScore>
i "Quartet" 0 20
</CsScore>
</CsoundSynthesizer>
