# watchmeter

## Abstract

Display an array of k-rate levels as a bar meter with one bar per channel.

## Description

`watchmeter` creates a meter window and feeds it, without a separate
[`watchadd`](watchadd.md) call. Each call creates a separate window. The length
of the levels array is the number of channels: an array of four elements
produces four bars, labelled `ch1` through `ch4` under the horizontal axis.

The values are plotted as they arrive. `watchmeter` measures nothing and
converts nothing: whatever the orchestra puts in the array is what the bars
show, so the array is expected to hold levels already, typically the output of
[`rms`](https://csound.com/manual/opcodes/rms.html) or
[`follow`](https://csound.com/manual/opcodes/follow.html). The `scale` argument
only declares which unit those numbers are expressed in, which selects the name
of the vertical axis.

The vertical range is fixed by `min_value` and `max_value` and never rescales to
the incoming levels: a meter is read against a reference, and a reference that
moves is not one. Above `max_value` the axis carries one grid division of
headroom, tinted like the peak marks, so that a level going over is seen going
over: a bar pinned to the top edge would look the same whether it exceeded the
range by a hair or by twenty decibels. Only past the headroom is the value
clamped. With the default eight divisions the headroom is an eighth of the
range, so a `-60` through `0` axis reaches `7.5` and a `0` through `1` axis
reaches `1.125`; the tick step is unchanged and the axis simply carries one more
of them.

The bar carries the level as it arrives, with no smoothing of its own. Each
packet already holds the loudest value of one viewer frame, so averaging it
again would only make the bar trail the sound. What holds is the peak mark above
it: it keeps the loudest recent value for about 1.5 seconds and then falls
towards the bottom of the axis, which is silence whatever the unit.

All bars use the same color. They are told apart by their position under the
channel names; a different hue per bar would suggest a difference in kind that
is not there. The peak marks have a color of their own, opposite the bars in hue
and pushed past them in lightness: a mark sits on top of the bar it belongs to,
and while the level is rising the two coincide, so a shade of the bar color
would vanish exactly when the peak is being set. Both colors follow the graph
theme.

Levels are reduced to one packet per viewer frame, `ceil(kr / 60)` control
cycles at a time. The value sent is the **loudest** of each window, so a peak
falling between two frames is still displayed. The maximum is the correct
reduction in both units, being monotonic.

The grid is horizontal only, in eight divisions, and is not configurable. See
the [watch overview](index.md) for process and communication details.

## Syntax

```csound
graph:i = watchmeter(levels:k[], min_value:i, max_value:i, scale:i)
graph:i = watchmeter(levels:k[], min_value:i, max_value:i, scale:i, title:S)
```

## Arguments

* `levels:k[]`: one dimensional k-rate array holding one level per channel,
  already expressed in the unit named by `scale`. It must hold from 1 through
  32 elements. Its length is the number of bars.
* `min_value:i`: bottom of the vertical axis. It must be smaller than
  `max_value`. Levels at or below it are drawn as silence.
* `max_value:i`: reference maximum. The plot continues one grid division above
  it as headroom, so going over the range remains visible.
* `scale:i`: unit of the incoming levels. `0` for linear gain, labelling the
  axis `Gain`, or `2` for decibels, labelling it `Level (dB)`. `1`, linear
  power, is not accepted: a meter is read either as a gain or in decibels.
* `title:S` (optional): window title. The default title is
  `Csound Signal-Watcher`. A title must occupy fewer than 384 bytes.

## Output

* `graph:i`: positive graph handle, accepted by [`watchtheme`](watchtheme.md).
  `0` indicates that the graph could not be registered; in that case the opcode
  stays silent instead of failing the note. Unlike the other graph opcodes the
  handle is not used to attach signals, since `watchmeter` feeds its own window.

## Execution Time

* Init
* Performance (k-rate)

A reinit replaces the meter: the previous graph is dropped and its window
closed, so nothing accumulates, but opening and closing windows is by far the
most expensive thing the viewer does. Create the meter once and let the levels
change. See the [watch overview](index.md) for the measured cost.

## Example

The complete example is available as
[`examples/watchmeter.csd`](../examples/watchmeter.csd).

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

instr Voices
    low:a = vco2(0.30, 110) * oscili(1, 0.25)
    high:a = vco2(0.18, 440) * oscili(1, 0.4)

    levels:k[] init 2
    levels[0] = dbfsamp(rms(low) + 0.000001)
    levels[1] = dbfsamp(rms(high) + 0.000001)

    graph:i = watchmeter(levels, -60, 0, 2, "Voice levels")

    mix:a = low + high
    out(mix, mix)
endin

</CsInstruments>
<CsScore>
i "Voices" 0 20
</CsScore>
</CsoundSynthesizer>
```

## See also

* [`watchcontrol`](watchcontrol.md)
* [`watchpoint`](watchpoint.md)
* [`watchtheme`](watchtheme.md)
* [watch overview](index.md)

## Credits

Pasquale Mainolfi, 2026
