# watchscope

## Abstract

Create a resizable oscilloscope graph for one or more a-rate signals.

## Description

`watchscope` creates a time-domain graph and returns a handle used by
[`watchadd`](watchadd.md). Each call creates a separate window. More than one
a-rate signal can be attached to the same handle; the viewer places those
streams on a shared sample timeline so their relative phase is preserved.

`win_size` specifies the visible time interval in seconds. The newest samples
are placed at the right edge of the graph and the signal scrolls towards the
left.

The nominal number of visible samples is `win_size * sample_rate`. The tick
arguments only divide the axes and grid; they do not control the number of
samples collected or displayed. For example, `x_ticks = 10` means 10 equal
time-axis intervals, not 10 samples.

The x-axis is labelled `Time (s)` and the y-axis `Amplitude`. When the y range is
omitted it defaults to `-1` through `1`.

Creating the first graph starts the standalone viewer automatically if another
viewer is not already listening. See the [watch overview](index.md) for process
and communication details.

## Syntax

```csound
graph:i = watchscope(win_size:i [, x_ticks:i [, y_ticks:i [, ymin:i [, ymax:i [, title:S]]]]])
```

## Arguments

* `win_size:i`: visible signal duration in seconds. Must be greater than zero.
* `x_ticks:i` (optional): whole number of horizontal axis/grid divisions, from
  `0` through `256`. It affects only the grid and tick labels, not the number of
  samples. `0` selects the viewer default of 10.
* `y_ticks:i` (optional): whole number of vertical axis/grid divisions, from
  `0` through `256`. It affects only the grid and tick labels, not the number of
  samples. `0` selects the viewer default of 8.
* `ymin:i` (optional): lower amplitude limit. When supplied without `ymax`, the
  upper limit is `ymin + 2`.
* `ymax:i` (optional): upper amplitude limit. It must be greater than `ymin`.
* `title:S` (optional): window title. The default title is
  `Csound Signal-Watcher`. A title must occupy fewer than 384 bytes.

## Output

* `graph:i`: positive graph handle passed to [`watchadd`](watchadd.md). `0`
  indicates that the graph could not be registered.

## Execution Time

* Init

## Examples

The complete example is also available as
[`examples/watchscope.csd`](../examples/watchscope.csd).

```csound
<CsoundSynthesizer>
<CsOptions>
-odac
</CsOptions>
<CsInstruments>

; -----------------------------------------------------------------------------
; watchscope.csd
;
; Create one oscilloscope window and attach two synchronized audio signals.
; The viewer is launched automatically and exits shortly after Csound stops.
; -----------------------------------------------------------------------------

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

instr Scope
    graph:i = watchscope(1, 10, 8, -1, 1, "Two oscillators")

    first:a = oscili(0.7, 2)
    second:a = oscili(0.3, 5)

    watchadd(graph, first)
    watchadd(graph, second)

    out(first + second, first + second)
endin

</CsInstruments>
<CsScore>
i "Scope" 0 20
</CsScore>
</CsoundSynthesizer>
```

## See also

* [`watchadd`](watchadd.md)
* [`watchcontrol`](watchcontrol.md)
* [`watchspectrum`](watchspectrum.md)
* [`watchspectrogram`](watchspectrogram.md)
* [watch overview](index.md)

## Credits

Pasquale Mainolfi, 2026
