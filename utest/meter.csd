<CsoundSynthesizer>
<CsOptions>
-odac -m0 --run-unit-tests
</CsOptions>
<CsInstruments>
sr = 44100
ksmps = 1
nchnls = 2
0dbfs = 1

; -----------------------------------------------------------------------------
; meter.csd
;
; Bar meters. What each window must show is written in its title:
;
;   * the number of bars is the length of the array, and the names ch1..chN sit
;     under the centre of their own bar;
;   * every bar has the same color, and each one carries a peak mark that holds
;     the loudest recent value before following the bar down;
;   * the axis carries one division of headroom above the declared maximum,
;     tinted like the peak marks: a level going over is seen going over, and
;     only beyond the headroom does the bar pin to the top edge;
;   * silence rests on the bottom of the axis;
;   * the axis is named Level (dB) or Gain according to the declared unit, and
;     it never rescales to the incoming levels.
;
; The bars carry the level as received, with no smoothing of their own; only the
; peak marks hold and then fall. One packet is published per viewer frame,
; carrying the loudest value of the window: at kr = 689 Hz that is 12 control
; cycles reduced to a single frame.
; -----------------------------------------------------------------------------

instr Decibel
    ; four channels a quarter cycle apart, so the bars never move together
    levels:k[] init 4
    phase:k = phasor(0.4)
    levels[0] = dbfsamp(0.9 * (0.5 - 0.5 * cos(6.283185307 * phase)) + 0.00001)
    levels[1] = dbfsamp(0.9 * (0.5 - 0.5 * cos(6.283185307 * (phase + 0.25))) + 0.00001)
    levels[2] = dbfsamp(0.9 * (0.5 - 0.5 * cos(6.283185307 * (phase + 0.5))) + 0.00001)
    levels[3] = dbfsamp(0.9 * (0.5 - 0.5 * cos(6.283185307 * (phase + 0.75))) + 0.00001)

    graph:i = watchmeter(levels, -60, 0, 2, "4 bars in dB, ch1..ch4 under their bar")
    assert_true(graph != 0)
endin

instr Gain
    ; the third channel is constant: its bar and its peak mark must not move
    levels:k[] init 3
    phase:k = phasor(0.7)
    levels[0] = 0.5 - 0.5 * cos(6.283185307 * phase)
    levels[1] = abs(cos(6.283185307 * phase * 2))
    levels[2] = 0.25
    graph:i = watchmeter(levels, 0, 1, 0, "3 bars in gain, the third one still")
    assert_true(graph != 0)
endin

instr Single
    ; the narrowest meter: one bar, filling the plot area
    levels:k[] init 1
    levels[0] = 0.5 - 0.5 * cos(6.283185307 * phasor(0.5))
    graph:i = watchmeter(levels, 0, 1, 0, "one bar, ch1 alone")
    assert_true(graph != 0)
endin

instr Crowded
    ; the widest meter: with 32 bars the names thin out, ch1 and ch32 remain
    levels:k[] init 32
    phase:k = phasor(0.25)
    kndx = 0
    while kndx < 32 do
        levels[kndx] = 0.5 - 0.5 * cos(6.283185307 * (phase + kndx / 32))
        kndx += 1
    od
    graph:i = watchmeter(levels, 0, 1, 0, "32 bars, ch1 and ch32 still readable")
    assert_true(graph != 0)
endin

instr OutOfRange
    ; the meter never rescales: the levels leave the range, the bars do not
    levels:k[] init 3
    phase:k = phasor(0.3)
    levels[0] = 2 * (0.5 - 0.5 * cos(6.283185307 * phase))  ; crosses 1, enters the headroom
    levels[1] = -0.5                                        ; below the range, silent
    levels[2] = 0                                           ; exactly the bottom
    graph:i = watchmeter(levels, 0, 1, 0, "ch1 goes into the headroom, no rescale")
    assert_true(graph != 0)
endin

</CsInstruments>
<CsScore>
i "Decibel"    0 15
i "Gain"       0 15
i "Single"     0 15
i "Crowded"    0 15
i "OutOfRange" 0 15
e
</CsScore>
</CsoundSynthesizer>
