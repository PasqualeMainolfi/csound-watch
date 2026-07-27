# watch

## Overview

**watch** is a Csound 7 plugin for real-time signal visualization. Graphs are
declared with an init-time opcode and populated with one or more signals through
`watchadd`. A meter is the exception: it is fed by a single array of levels, so
`watchmeter` owns its window and needs no `watchadd`.

The current implementation provides:

* oscilloscope-style plots for a-rate signals;
* time-domain plots for k-rate control signals;
* power spectra from non-sliding `f`-signals;
* spectrograms from non-sliding `f`-signals;
* static plots of complete function tables;
* moving points on a fixed plane from k-rate coordinate pairs;
* bar meters from k-rate arrays of levels, with peak hold;
* selectable light and dark graph themes.

## Architecture

Plotting is performed by a standalone SDL3 viewer rather than by Csound's audio
thread.

During performance, `watchadd` copies audio samples, control values, or spectral
bins into a bounded, preallocated ring buffer. Control values are accumulated
into 256-sample packets, with the final residual packet published when the
attaching instrument instance ends. Point streams publish once per viewer frame
instead, because the eye compares a moving point against the present. A meter
packet carries a single frame, so `watchmeter` reduces the control rate itself:
it keeps the loudest value of each `ceil(kr / 60)` cycles and publishes that. A
dedicated sender thread reads the buffer and sends small binary packets over UDP
to `127.0.0.1:48120`. The viewer receives the packets, reconstructs
each signal window or spectral frame, and renders it in a separate process.

The performance opcode does not perform socket I/O, allocate viewer memory, or
wait for rendering. If a ring buffer fills, new data can be dropped instead of
blocking audio performance.

The viewer acknowledges each graph configuration before the sender begins
streaming its data. Configuration packets are retried until that acknowledgement
arrives, including for a graph whose note has already ended: a table plotted by
a note shorter than the viewer takes to start still appears. The only graph that
stops being retried is one replaced by a `reinit`, which has nothing left to
show.

Function tables use a finite-transfer path rather than the performance ring
buffer. `watchtable` captures a snapshot at init time, and the sender divides it
into offset-addressed chunks after configuration acknowledgement. The viewer
publishes the plot only after all samples have arrived.

## Viewer lifecycle

Whenever a graph is created, the viewer asks the desktop window manager to
bring the new window to the foreground and assign it input focus. A window
manager may reject this request according to its focus-stealing policy. New
windows are arranged in a grid across the usable display area so simultaneous
graphs remain visible; grid positions are reused when all cells are occupied.

The plugin automatically starts `watch_viewer` when the first graph is created
and no existing viewer answers the configuration probe.

The executable is resolved in this order:

1. the path in the `CSOUND_WATCH_VIEWER` environment variable;
2. `watch_viewer` (`watch_viewer.exe` on Windows) next to the plugin;
3. the standard Risset asset directory;
4. the executable found through `PATH`.

One viewer can serve several graphs and several Csound processes. UDP sender
endpoints distinguish sessions even when their graph handles have the same
numeric value.

Closing a graph window manually removes that graph from the viewer. Closing the
last window manually exits the viewer immediately. When a Csound session
A window outlives the note that created it: the graph is dropped when its
instrument instance ends, but what it shows stays readable until the session
closes. A graph replaced by a `reinit` is the exception, since the instrument
goes on with a new graph and the previous window no longer stands for anything:
that one is closed.

Reinitializing a graph opcode is handled, but it is not a good way to use one.
Each pass discards a window and opens another, and a window carries an
operating-system window, a renderer and its font textures: building and tearing
that down costs far more than anything else the viewer does. At twenty
reinitializations per second the viewer takes around 59% of a CPU core, against
about 2% in normal use, and closing lags behind opening. Graphs are meant to be
created once, outside reinit blocks, and fed changing signals.

When a Csound session terminates, the plugin sends a session-close message: only windows belonging to
that session are removed. If no windows remain, the viewer waits 500 ms before
exiting. A new graph arriving during that interval cancels the shutdown.

## Opcode summary

| Opcode | Purpose |
|---|---|
| [`watchscope`](watchscope.md) | Create a time-domain oscilloscope graph |
| [`watchcontrol`](watchcontrol.md) | Create a k-rate control-signal graph |
| [`watchspectrum`](watchspectrum.md) | Create a power-spectrum graph |
| [`watchspectrogram`](watchspectrogram.md) | Create a scrolling spectrogram |
| [`watchtable`](watchtable.md) | Plot a complete function table |
| [`watchpoint`](watchpoint.md) | Create a fixed plane for moving points |
| [`watchmeter`](watchmeter.md) | Display an array of levels as a bar meter |
| [`watchtheme`](watchtheme.md) | Select a graph's light or dark theme |
| [`watchadd`](watchadd.md) | Attach a signal or a coordinate pair to a graph |

## Common behavior and limits

* Graph handles are positive init-rate values. A value of `0` means that the
  graph could not be registered.
* A Csound instance can register up to 32 graphs.
* A graph can contain up to 64 concurrent streams. A stream slot is released
  when the instrument instance that created it ends and its queued packets have
  been sent.
* Tick counts are whole numbers from `0` through `256`. A value outside that
  range is an init error.
* Several compatible signals can be attached to the same graph. Time-domain
  streams share one display timeline so that their phase relationship is
  preserved.
* Graph windows are resizable. Axis names and tick labels are generated by the
  viewer.
* The vertical centre of a plot is the centre of its y range, not the value `0`.
  The zero line is drawn only when the range contains zero, at the relative
  height `(0 - ymin) / (ymax - ymin)`: on the bottom edge for `0..1`, at the
  centre for `-1..1`, and not at all for a range such as `20..20000`.
* A point graph draws a fixed plane: the axes never rescale to the incoming
  coordinates, the plot area is kept square so that circular paths are not
  drawn as ellipses, and each point keeps a trail of about 100 ms.
* A meter graph draws one bar per channel, from 1 through 32, and its vertical
  range never rescales either. It continues one grid division above the declared
  maximum as headroom, so a level going over remains visible; past that it is
  clamped. The levels are plotted in the unit they arrive in, since a meter
  displays a measurement made by the orchestra rather than making one.
* A graph opcode inside a reinit block replaces its graph on every pass. The
  previous graph is dropped and its window closed, so nothing accumulates, but
  the cost of opening and closing windows makes this an expensive way to work:
  create the graph once and let the signal change.
* Communication is local UDP. It is intended for visualization, not lossless
  recording.

## Building

Configure and build both components:

```sh
cmake -S . -B build \
  -DBUILD_WATCH_OPCODES=ON \
  -DBUILD_WATCH_VIEWER=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --target watch watch_viewer -j4
```

The default build uses the vendored SDL3, SDL3_ttf and FreeType sources. They are
linked statically into the viewer; the Csound plugin itself does not link to SDL.

For a local test, point Csound's opcode directory at the build directory. The
plugin will find the sibling viewer automatically:

```sh
OPCODE7DIR64="$PWD/build" csound example.csd
```

Complete `.csd` files for every opcode are available in the
[`examples`](../examples) directory. The [`utest`](../utest) directory holds the
per-area test orchestras; the ones with numeric checks report their result
through Csound's `--run-unit-tests` assertions.

## Credits

Pasquale Mainolfi, 2026
