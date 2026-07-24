<CsoundSynthesizer>
<CsOptions>
-odac
</CsOptions>
<CsInstruments>

; -----------------------------------------------------------------------------
; watchtable.csd
;
; Transfer a complete function table to the viewer and plot it only after all
; chunks have arrived. The size exceeds the stream ring-buffer capacity to
; exercise the dedicated finite-transfer path.
; -----------------------------------------------------------------------------

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

wave@global:i = ftgen(0, 0, 65536, 10, 1, 0.5, 0.25, 0.125)
watchtable(wave, -1, 1, "Function table")

instr PlotTable
endin

</CsInstruments>
<CsScore>
i "PlotTable" 0 10
</CsScore>
</CsoundSynthesizer>
