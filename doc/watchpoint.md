# watchpoint

## Abstract

Create a fixed plane on which one or more k-rate coordinate pairs are drawn as
moving points.

## Description

`watchpoint` creates a plane graph and returns a handle used by
[`watchadd`](watchadd.md). Each call creates a separate window. More than one
coordinate pair can be attached to the same handle; every pair becomes a
separate point with its own color.

Unlike the time-domain graphs, neither axis represents time: `x` and `y` are
spatial coordinates and the plane does not scroll. The limits passed to the
opcode define the visible plane once and for all, so a point that leaves the
range disappears at the border instead of rescaling the axes.

The viewer keeps a short trail behind each point, about 100 ms long. The trail
fades towards its oldest end, which makes the direction of the movement
readable from a single frame. Its length is fixed by the viewer and expressed
in seconds, so it looks the same at any control rate; the number of retained
points is derived from `kr`.

The vertical centre of the plane is the centre of the `y` range and the
horizontal centre is the centre of the `x` range. A zero axis is drawn only
when the corresponding range contains zero, at the relative position
`(0 - min) / (max - min)`. A plane of `-1` through `1` therefore shows a cross
at its centre, while a plane of `20` through `20000` shows no axis at all.

The plot area is always square, whatever the window shape: equal units per
pixel on both axes are required for a circular path to be drawn as a circle
rather than an ellipse. The grid uses eight divisions per axis and is not
configurable.

Point streams are published once per viewer frame instead of waiting for a full
packet, because a moving point is compared against the present by the eye. See
the [watch overview](index.md) for process and communication details.

## Syntax

```csound
graph:i = watchpoint(xmin:i, xmax:i, ymin:i, ymax:i)
graph:i = watchpoint(xmin:i, xmax:i, ymin:i, ymax:i, title:S)
```

## Arguments

* `xmin:i`: left edge of the plane. It must be smaller than `xmax`.
* `xmax:i`: right edge of the plane.
* `ymin:i`: bottom edge of the plane. It must be smaller than `ymax`.
* `ymax:i`: top edge of the plane.
* `title:S` (optional): window title. The default title is
  `Csound Signal-Watcher`. A title must occupy fewer than 384 bytes.

## Output

* `graph:i`: positive graph handle passed to [`watchadd`](watchadd.md). `0`
  indicates that the graph could not be registered.

## Execution Time

* Init

## Example

The complete example is available as
[`examples/watchpoint.csd`](../examples/watchpoint.csd).

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

instr Lissajous
    graph:i = watchpoint(-1, 1, -1, 1, "Lissajous")

    phase:k = phasor(0.25)
    x:k = cos(phase * 2 * $M_PI * 3)
    y:k = sin(phase * 2 * $M_PI * 2)

    watchadd(graph, x, y)
endin

</CsInstruments>
<CsScore>
i "Lissajous" 0 20
</CsScore>
</CsoundSynthesizer>
```

## See also

* [`watchadd`](watchadd.md)
* [`watchcontrol`](watchcontrol.md)
* [`watchtheme`](watchtheme.md)
* [watch overview](index.md)

## Credits

Pasquale Mainolfi, 2026
