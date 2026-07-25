<CsoundSynthesizer>
<CsOptions>
-odac -m0
</CsOptions>
<CsInstruments>

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1


graph@global:i = watchcontrol(1, 10, 8, -1, 1, "Control signals")

instr ControlStreams
    assert_true(graph != 0)

    sine:k = oscili(0.8, 2)
    phase:k = phasor(0.5)
    ramp:k = phase * 2 - 1
    stepped:k = int(phase * 8) * 0.25 - 1

    watchadd(graph, sine)
    watchadd(graph, ramp)
    watchadd(graph, stepped)
endin

instr ResidualControlBlock
    graph:i = watchcontrol(0.1, 5, 5, -1, 1, "Residual control block")
    assert_true(graph != 0)

    value:k = linseg(-1, p3, 1)
    watchadd(graph, value)
endin

</CsInstruments>
<CsScore>
f0 3600
; At kr = 1500 Hz, this produces several complete 256-sample packets and a
; final partial packet for each stream.
i "ControlStreams" 0 5

; 0.1 seconds produces only 150 control samples, so the entire signal is sent
; by the residual-block flush in watch_deinit().
i "ResidualControlBlock" 0.5 0.1
e
</CsScore>
</CsoundSynthesizer>
