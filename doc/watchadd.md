# watchadd

## Abstract

Attach an a-rate, k-rate, or `f`-signal to a compatible watch graph.

## Description

`watchadd` connects a signal to a graph created by
[`watchscope`](watchscope.md), [`watchcontrol`](watchcontrol.md),
[`watchspectrum`](watchspectrum.md), or
[`watchspectrogram`](watchspectrogram.md).

The opcode is selected automatically from the signal type:

* an a-rate signal can be attached to `watchscope`;
* a k-rate signal can be attached to `watchcontrol`;
* an `f`-signal can be attached to `watchspectrum` or `watchspectrogram`.

Several compatible signals can be attached to one graph. Each call creates a
separate stream, up to 64 concurrent streams per graph.

A stream belongs to the instrument instance that created it. When that instance
ends, its residual packet is published and the stream slot is returned to the
graph as soon as the sender has drained it, so a graph created once in the
orchestra header can serve an unlimited number of notes over time. The graph
itself stays alive until every stream attached to it has been released, so a
`watchadd` still running after its graph creator has been deinitialized keeps
working until its own note ends.

### Audio streams

Audio samples are accumulated into packets containing at most 256 samples and
written to a bounded single-producer/single-consumer ring. This remains efficient
when `ksmps = 1` and splits larger control blocks across as many packets as
required.

Packets carry sample-position timestamps. The viewer uses one timeline for all
audio streams belonging to the graph, preserving their phase relationship.

### Control streams

One k-rate value is collected per control cycle. Values are accumulated into
packets of 256 samples before they are published to the sender. A discontinuity
publishes the partial packet before accumulation resumes at the new control
timestamp. When the instance ends, its final residual packet is also published
and the stream remains alive until the sender has drained its queue.

This batching adds up to approximately `256 / kr` seconds of latency before a
complete packet becomes available to the sender. Packets carry control-cycle
timestamps so compatible streams in the same graph share a timeline.

### Spectral streams

The `f`-signal must be non-sliding and use one of the PVS formats supported by
`pvsanal`: amplitude/frequency, amplitude/phase, or complex. Sliding PVS signals
are not currently supported.

Watch converts every bin to power. Large frames are divided across packets and
reassembled by the viewer before they are displayed. The graph's scale determines
whether the viewer shows gain, power, or decibels.

### Real-time behavior

`watchadd` never renders or sends UDP from its performance callback. It only
copies data into a preallocated bounded queue; a dedicated sender thread handles
communication. If that queue is full, data is dropped rather than blocking the
audio thread.

## Syntax

```csound
watchadd(graph:i, signal:a)
watchadd(graph:i, signal:k)
watchadd(graph:i, signal:f)
```

## Arguments

* `graph:i`: handle returned by a graph-creation opcode. Its domain must match
  the signal type.
* `signal:a`: audio signal attached to a `watchscope` graph.
* `signal:k`: control signal attached to a `watchcontrol` graph.
* `signal:f`: non-sliding PVS signal attached to a `watchspectrum` or
  `watchspectrogram` graph.

## Output

None.

## Execution Time

* Init + Performance

## Examples

The complete example is also available as
[`examples/watchadd.csd`](../examples/watchadd.csd).

```csound
<CsoundSynthesizer>
<CsOptions>
-odac
</CsOptions>
<CsInstruments>

; -----------------------------------------------------------------------------
; watchadd.csd
;
; Attach one audio signal to an oscilloscope and its PVS representation to both
; a spectrum and a spectrogram.
; -----------------------------------------------------------------------------

sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

instr WatchSignals
    scope:i = watchscope(0.5, 10, 8, -1, 1, "Waveform")
    control:i = watchcontrol(2, 10, 8, -1, 1, "Control signal")
    spectrum:i = watchspectrum(20, 12000, -120, 0, 2, 10, 8, "Spectrum")
    spectrogram:i = watchspectrogram(3, 20, 12000, -120, 0, 2, 8, 8, "Spectrogram")

    signal:a = vco2(0.35, 220)
    modulation:k = oscili(0.8, 1)
    analysis:f = pvsanal(signal, 2048, 256, 2048, 1)

    watchadd(scope, signal)
    watchadd(control, modulation)
    watchadd(spectrum, analysis)
    watchadd(spectrogram, analysis)

    out(signal, signal)
endin

</CsInstruments>
<CsScore>
i "WatchSignals" 0 20
</CsScore>
</CsoundSynthesizer>
```

## See also

* [`watchscope`](watchscope.md)
* [`watchcontrol`](watchcontrol.md)
* [`watchspectrum`](watchspectrum.md)
* [`watchspectrogram`](watchspectrogram.md)
* [watch overview](index.md)

## Credits

Pasquale Mainolfi, 2026
