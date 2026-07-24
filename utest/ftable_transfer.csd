<CsoundSynthesizer>
<CsOptions>
-odac
</CsOptions>
<CsInstruments>
sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

table@global:i = ftgen(0, 0, 65536, 10, 1, 0.5, 0.25, 0.125)
watchtable(table, -1, 1, "Deferred ftable cleanup test")

instr KeepSessionAlive
endin


</CsInstruments>
<CsScore>
; PlotTable deinitializes before the transfer can complete. The sender must
; retain the graph until all 256 packets have been sent.
i "KeepSessionAlive" 0 10
</CsScore>
</CsoundSynthesizer>
