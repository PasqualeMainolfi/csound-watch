# watchspectrogram

## Abstract

Create a scrolling spectrogram for one or more non-sliding `f`-signals.

## Description

`watchspectrogram` creates a time-frequency graph and returns a handle used by
[`watchadd`](watchadd.md). Attach an `f`-signal produced by an opcode such as
`pvsanal`. Every complete PVS frame becomes one column in the spectrogram.

`history_seconds` controls the visible history. The newest frame is placed at
the right edge; older frames scroll left. The x-axis covers
`-history_seconds` through `0` and is labelled `Time (s)`. The y-axis is
frequency in hertz.

Spectral power can be visualized as linear gain, linear power, or decibels:

* scale `0`: gain, calculated as `sqrt(power)`;
* scale `1`: power;
* scale `2`: decibels, calculated as `10 * log10(power)` and limited to a
  `-160 dB` floor.

Higher values are rendered darker and lower values lighter. Default value
ranges are `0` through `1` for the linear scales and `-120` through `0` for
decibels. The automatic frequency range is `0 Hz` through Nyquist.

The frequency axis is currently linear.

The tick arguments only divide the axes and grid. They do not change the number
of PVS frames retained in the history or the number of spectral bins.

## Syntax

```csound
graph:i = watchspectrogram(history_seconds:i)
graph:i = watchspectrogram(history_seconds:i, min_freq:i, max_freq:i, min_value:i, max_value:i)
graph:i = watchspectrogram(history_seconds:i, min_freq:i, max_freq:i, min_value:i, max_value:i, scale:i [, x_ticks:i [, y_ticks:i]])
graph:i = watchspectrogram(history_seconds:i, min_freq:i, max_freq:i, min_value:i, max_value:i, scale:i, x_ticks:i, y_ticks:i, title:S)
```

## Arguments

* `history_seconds:i`: visible spectrogram history in seconds. Must be greater
  than zero.
* `min_freq:i`: minimum displayed frequency in Hz. Must lie between `0` and
  Nyquist.
* `max_freq:i`: maximum displayed frequency in Hz. Must be greater than
  `min_freq` and no greater than Nyquist.
* `min_value:i`: lower displayed value in the selected scale. Linear scales
  require non-negative values.
* `max_value:i`: upper displayed value in the selected scale. Must be greater
  than `min_value`.
* `scale:i`: spectral display scale: `0` for gain, `1` for power, or `2` for
  decibels.
* `x_ticks:i` (optional): whole number of time axis/grid divisions, from `0`
  through `256`. `0` selects the viewer default of 10.
* `y_ticks:i` (optional): whole number of frequency axis/grid divisions, from
  `0` through `256`; a value outside that range is an init error. `0` selects
  the viewer default of 8.
* `title:S` (optional): window title. The default title is
  `Csound Signal-Watcher`. A title must occupy fewer than 384 bytes.

To use automatic frequency and value ranges, supply only `history_seconds`.

## Output

* `graph:i`: positive graph handle passed to [`watchadd`](watchadd.md). `0`
  indicates that the graph could not be registered.

## Execution Time

* Init

## Examples

The complete example is also available as
[`examples/watchspectrogram.csd`](../examples/watchspectrogram.csd).

```csound
<CsoundSynthesizer>
<CsOptions>
-odac
</CsOptions>
<CsInstruments>

; -----------------------------------------------------------------------------
; watchspectrogram.csd
;
; Display four seconds of spectral history for a slowly swept oscillator.
; -----------------------------------------------------------------------------

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

instr Spectrogram
    graph:i = watchspectrogram(4, 20, 12000, -120, 0, 2, 8, 8, "Frequency sweep")

    frequency:k = expon(100, p3, 6000)
    signal:a = vco2(0.3, frequency)
    analysis:f = pvsanal(signal, 2048, 256, 2048, 1)

    watchadd(graph, analysis)
    out(signal, signal)
endin

</CsInstruments>
<CsScore>
i "Spectrogram" 0 20
</CsScore>
</CsoundSynthesizer>
```

## See also

* [`watchadd`](watchadd.md)
* [`watchspectrum`](watchspectrum.md)
* [`watchscope`](watchscope.md)
* [watch overview](index.md)

## Credits

Pasquale Mainolfi, 2026
