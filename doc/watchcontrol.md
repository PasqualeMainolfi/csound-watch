# watchcontrol

## Abstract

Create a resizable time-domain graph for one or more k-rate control signals.

## Description

`watchcontrol` creates a control-signal graph and returns a handle used by
[`watchadd`](watchadd.md). Each call creates a separate window. More than one
k-rate signal can be attached to the same handle and displayed on a shared
control-sample timeline.

`win_size` specifies the visible interval in seconds. The newest values are
placed at the right edge of the graph and older values scroll towards the left.
The nominal number of visible values is `win_size * kr`.

One value is collected on every k-cycle. Watch accumulates 256 values before
publishing a complete packet to the sender thread. A control-timeline
discontinuity publishes the current partial packet, and the end of the
attaching instrument instance publishes the final residual packet. This
batching adds up to approximately `256 / kr`
seconds of latency before a new complete packet becomes available to the
sender.

The tick arguments divide only the axes and grid; they do not determine how
many control values are collected. The x-axis is labelled `Time (s)` and the
y-axis `Value`. When the y range is omitted it defaults to `-1` through `1`.

Creating the first graph starts the standalone viewer automatically if another
viewer is not already listening. See the [watch overview](index.md) for process
and communication details.

## Syntax

```csound
graph:i = watchcontrol(win_size:i [, x_ticks:i [, y_ticks:i [, ymin:i [, ymax:i [, title:S]]]]])
```

## Arguments

* `win_size:i`: visible signal duration in seconds. Must be greater than zero.
* `x_ticks:i` (optional): whole number of horizontal axis/grid divisions, from
  `0` through `256`; a value outside that range is an init error. It affects
  only the grid and tick labels. `0` selects the viewer default of 10.
* `y_ticks:i` (optional): whole number of vertical axis/grid divisions, from
  `0` through `256`; a value outside that range is an init error. It affects
  only the grid and tick labels. `0` selects the viewer default of 8.
* `ymin:i` (optional): lower value limit. When supplied without `ymax`, the
  upper limit is `ymin + 2`.
* `ymax:i` (optional): upper value limit. It must be greater than `ymin`.
* `title:S` (optional): window title. The default title is
  `Csound Signal-Watcher`. A title must occupy fewer than 384 bytes.

## Output

* `graph:i`: positive graph handle passed to [`watchadd`](watchadd.md). `0`
  indicates that the graph could not be registered.

## Execution Time

* Init

## Example

The complete example is available as
[`examples/watchcontrol.csd`](../examples/watchcontrol.csd).

```csound
<CsoundSynthesizer>
<CsOptions>
-odac -m0
</CsOptions>
<CsInstruments>

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

instr ControlSignals
    graph:i = watchcontrol(2, 10, 8, -1, 1, "Control signals")

    sine:k = oscili(0.8, 1)
    phase:k = phasor(0.25)
    ramp:k = phase * 2 - 1

    watchadd(graph, sine)
    watchadd(graph, ramp)
endin

</CsInstruments>
<CsScore>
i "ControlSignals" 0 20
</CsScore>
</CsoundSynthesizer>
```

## See also

* [`watchadd`](watchadd.md)
* [`watchscope`](watchscope.md)
* [`watchtheme`](watchtheme.md)
* [watch overview](index.md)

## Credits

Pasquale Mainolfi, 2026
