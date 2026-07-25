# watchspectrum

## Abstract

Create a power-spectrum graph for one or more non-sliding `f`-signals.

## Description

`watchspectrum` creates a frequency-domain graph and returns a handle used by
[`watchadd`](watchadd.md). Attach an `f`-signal produced by an opcode such as
`pvsanal`; each new PVS frame replaces the spectrum currently displayed.

Internally, watch transmits spectral power. The viewer can display that power as
linear gain, linear power, or decibels:

* scale `0`: gain, calculated as `sqrt(power)`;
* scale `1`: power;
* scale `2`: decibels, calculated as `10 * log10(power)` and limited to a
  `-160 dB` floor.

The default scale is linear gain. Default value ranges are `0` through `1` for
the linear scales and `-120` through `0` for decibels. When the frequency range
is omitted, the viewer uses `0 Hz` through Nyquist after the first spectral frame
arrives.

The frequency axis is currently linear.

The tick arguments only divide the axes and grid. They do not change the FFT
size, the number of spectral bins, or the displayed frequency range.

## Syntax

```csound
graph:i = watchspectrum()
graph:i = watchspectrum(min_freq:i, max_freq:i, min_value:i, max_value:i)
graph:i = watchspectrum(min_freq:i, max_freq:i, min_value:i, max_value:i, scale:i [, x_ticks:i [, y_ticks:i]])
graph:i = watchspectrum(min_freq:i, max_freq:i, min_value:i, max_value:i, scale:i, x_ticks:i, y_ticks:i, title:S)
```

## Arguments

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
* `x_ticks:i` (optional): whole number of frequency axis/grid divisions, from
  `0` through `256`; a value outside that range is an init error. `0` selects
  the viewer default of 10.
* `y_ticks:i` (optional): whole number of value axis/grid divisions, from `0`
  through `256`. `0` selects the viewer default of 8.
* `title:S` (optional): window title. The default title is
  `Csound Signal-Watcher`. A title must occupy fewer than 384 bytes.

To use the automatic frequency and value ranges, call `watchspectrum()` with no
arguments.

## Output

* `graph:i`: positive graph handle passed to [`watchadd`](watchadd.md). `0`
  indicates that the graph could not be registered.

## Execution Time

* Init

## Examples

The complete example is also available as
[`examples/watchspectrum.csd`](../examples/watchspectrum.csd).

```csound
<CsoundSynthesizer>
<CsOptions>
-odac
</CsOptions>
<CsInstruments>

; -----------------------------------------------------------------------------
; watchspectrum.csd
;
; Analyze a harmonic signal with pvsanal and display its power spectrum in dB.
; -----------------------------------------------------------------------------

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

instr Spectrum
    graph:i = watchspectrum(20, 12000, -120, 0, 2, 10, 8, "Harmonic spectrum")

    signal:a = vco2(0.35, 220)
    analysis:f = pvsanal(signal, 2048, 512, 2048, 1)

    watchadd(graph, analysis)
    out(signal, signal)
endin

</CsInstruments>
<CsScore>
i "Spectrum" 0 20
</CsScore>
</CsoundSynthesizer>
```

## See also

* [`watchadd`](watchadd.md)
* [`watchspectrogram`](watchspectrogram.md)
* [`watchscope`](watchscope.md)
* [watch overview](index.md)

## Credits

Pasquale Mainolfi, 2026
