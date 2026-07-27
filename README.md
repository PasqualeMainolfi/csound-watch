# Csound Watch

Csound Watch is a Csound 7 plugin for real-time signal visualization. It
provides simple opcodes for creating oscilloscope, control-signal, spectrum,
spectrogram, static function-table, moving-point, and bar-meter windows while
keeping all graphics work outside Csound's audio thread.

Graphs are created with an init-time opcode and signals are attached with
`watchadd`:

```csound
graph:i = watchscope(1, 10, 8, -1, 1, "Oscilloscope")

first:a = oscili(0.7, 220)
second:a = oscili(0.3, 440)

watchadd(graph, first)
watchadd(graph, second)
```

`watchmeter` is the exception: a meter is fed by a single array of levels, so
the opcode owns its window and needs no `watchadd`.

Each graph is displayed in a separate, resizable SDL3 window. When a graph is
created, the viewer asks the desktop window manager to bring its window to the
foreground and give it input focus. New windows are arranged in a grid within
the usable area of the display so simultaneous graphs do not completely cover
one another. The standalone viewer is launched automatically when it is needed
and normally terminates shortly after Csound stops.

> [!NOTE]
> Csound Watch currently targets the Csound 7 plugin API.

## Features

- Oscilloscope-style visualization of a-rate signals.
- Time-domain visualization of k-rate control signals.
- Power-spectrum visualization of non-sliding PVS `f`-signals.
- Scrolling spectrograms from non-sliding PVS `f`-signals.
- Complete, init-time plotting of function tables.
- Moving points with a fading trail on a fixed, square plane.
- Multi-channel bar meters with peak hold and headroom above the range.
- Gain, power, and decibel spectral display scales.
- Several compatible streams in the same graph.
- Shared sample timeline for synchronized time-domain streams.
- Resizable windows with automatically generated axis and tick labels.
- Newly created graph windows are brought to the foreground.
- Per-graph light and dark themes with high-contrast stream colors.
- Automatic launch and shutdown of the standalone viewer.
- Support for several simultaneous Csound processes through one viewer.
- Portable local UDP transport for macOS, Linux, and Windows.
- No rendering, socket I/O, waiting, or viewer allocation in performance
  callbacks.

## How it works

Csound and the viewer run as separate processes:

```text
Csound audio thread
        |
        | copies audio samples, control values, or spectral bins
        v
bounded per-stream ring buffer
        |
        | dedicated sender thread
        v
UDP 127.0.0.1:48120
        |
        v
standalone SDL3 viewer
```

`watchadd` only copies incoming data into a bounded, preallocated queue. Audio
samples are packetized as they arrive. Control samples are accumulated into
batches of up to 256 values; a final partial batch is published when the
attaching instrument instance ends. A dedicated sender thread removes packets
from the queue and sends them to the viewer. If the queue is full, new
visualization data may be dropped rather than blocking Csound's performance
thread.

`watchtable` takes an init-time snapshot instead. The sender transfers that
snapshot progressively after the graph configuration is acknowledged, without
placing the complete table in the streaming ring buffer. The viewer assembles
chunks by offset and exposes the plot only after every table sample has arrived.

The viewer validates and acknowledges each graph configuration before the
sender starts streaming data for that graph. Audio and control packets are
accumulated into the requested time window, while spectral packets are
reassembled into complete PVS frames.

The transport is intended for responsive visualization, not lossless recording.

## Opcodes

| Opcode | Signal domain | Purpose |
|---|---|---|
| [`watchscope`](doc/watchscope.md) | Time | Create an oscilloscope graph |
| [`watchcontrol`](doc/watchcontrol.md) | Time | Create a control-signal graph |
| [`watchspectrum`](doc/watchspectrum.md) | Frequency | Create a power-spectrum graph |
| [`watchspectrogram`](doc/watchspectrogram.md) | Time/frequency | Create a scrolling spectrogram |
| [`watchtable`](doc/watchtable.md) | Table index | Plot a complete function table |
| [`watchpoint`](doc/watchpoint.md) | Plane | Create a fixed plane for moving points |
| [`watchmeter`](doc/watchmeter.md) | Level | Display an array of levels as a bar meter |
| [`watchtheme`](doc/watchtheme.md) | Graph appearance | Select the light or dark theme |
| [`watchadd`](doc/watchadd.md) | Any | Attach a signal or a coordinate pair to a graph |

### `watchscope`

```csound
graph:i = watchscope(win_size:i [, x_ticks:i [, y_ticks:i]])
graph:i = watchscope(win_size:i, x_ticks:i, y_ticks:i, ymin:i)
graph:i = watchscope(win_size:i, x_ticks:i, y_ticks:i, ymin:i, ymax:i)
graph:i = watchscope(win_size:i, x_ticks:i, y_ticks:i, ymin:i, ymax:i, title:S)
```

`win_size` is the visible duration in seconds. The default amplitude range is
`-1` through `1`. When only `ymin` is supplied, `ymax` is `ymin + 2`.

`x_ticks` and `y_ticks` control only how many intervals the corresponding axis
and grid are divided into. They do not determine the number of samples in the
window. For an audio stream, the nominal number of visible samples is:

```text
visible samples = win_size × sample rate
```

For example, `x_ticks = 10` divides the time axis into 10 equal intervals. It
does not limit the window to 10 samples. The value `0` requests the viewer
default; explicit values may range from 1 through 256.

Several a-rate signals may be attached to the same scope. Packets carry absolute
sample positions, allowing the viewer to preserve their relative phase even
with `ksmps = 1`, large control blocks, or variable packet arrival times.

### `watchcontrol`

```csound
graph:i = watchcontrol(win_size:i [, x_ticks:i [, y_ticks:i]])
graph:i = watchcontrol(win_size:i, x_ticks:i, y_ticks:i, ymin:i)
graph:i = watchcontrol(win_size:i, x_ticks:i, y_ticks:i, ymin:i, ymax:i)
graph:i = watchcontrol(win_size:i, x_ticks:i, y_ticks:i, ymin:i, ymax:i, title:S)
```

`watchcontrol` creates a time-domain graph for one or more k-rate signals.
`win_size` is the visible duration in seconds, and the default value range is
`-1` through `1`. The x-axis is labelled `Time (s)` and the y-axis `Value`.

One value is collected per k-cycle. Values are accumulated into packets of 256
samples before transmission; a discontinuity, or the end of the attaching
instrument instance, publishes a shorter final packet. Consequently, packet
batching adds up to approximately `256 / kr` seconds of latency before a new
batch becomes available to the sender. Tick arguments affect only the grid and
labels.

### `watchspectrum`

```csound
graph:i = watchspectrum()
graph:i = watchspectrum(min_freq:i, max_freq:i, min_value:i, max_value:i)
graph:i = watchspectrum(min_freq:i, max_freq:i, min_value:i, max_value:i, scale:i [, x_ticks:i [, y_ticks:i]])
graph:i = watchspectrum(min_freq:i, max_freq:i, min_value:i, max_value:i, scale:i, x_ticks:i, y_ticks:i, title:S)
```

Each complete PVS frame replaces the spectrum currently displayed. With no
arguments, the frequency range is selected automatically from `0 Hz` to Nyquist
after the first frame arrives.

The tick arguments affect only grid divisions and labels. They do not change the
FFT size, the number of spectral bins, or the selected frequency range.

### `watchspectrogram`

```csound
graph:i = watchspectrogram(history_seconds:i)
graph:i = watchspectrogram(history_seconds:i, min_freq:i, max_freq:i, min_value:i, max_value:i)
graph:i = watchspectrogram(history_seconds:i, min_freq:i, max_freq:i, min_value:i, max_value:i, scale:i [, x_ticks:i [, y_ticks:i]])
graph:i = watchspectrogram(history_seconds:i, min_freq:i, max_freq:i, min_value:i, max_value:i, scale:i, x_ticks:i, y_ticks:i, title:S)
```

`history_seconds` determines how much spectral history is visible. Each complete
PVS frame becomes one column, with the newest frame placed at the right edge.

The tick arguments affect only grid divisions and labels. They do not change the
number of PVS frames retained in the spectrogram history.

### `watchadd`

```csound
watchadd(graph:i, signal:a)
watchadd(graph:i, signal:k)
watchadd(graph:i, signal:f)
```

The overload is selected from the signal type:

- a-rate signals can be attached to `watchscope`;
- k-rate signals can be attached to `watchcontrol`;
- non-sliding `f`-signals can be attached to `watchspectrum` or
  `watchspectrogram`.

Supported PVS formats are amplitude/frequency, amplitude/phase, and complex.
Sliding PVS signals are not currently supported.

### `watchtable`

```csound
watchtable table:i
watchtable.m table:i, ymin:i
watchtable.mm table:i, ymin:i, ymax:i
watchtable.t table:i, ymin:i, ymax:i, theme:i
watchtable.s table:i, ymin:i, ymax:i, title:S
watchtable.ts table:i, ymin:i, ymax:i, theme:i, title:S
```

`watchtable` snapshots the selected function table at init time and sends it in
chunks. The x-axis contains table indices. A limit that is left out of the call
is derived from the completed table. The viewer does not expose a partial plot:
rendering begins only after every sample has been assembled. `theme` is `0`
for light and `1` for dark.

### `watchpoint`

```csound
graph:i = watchpoint(xmin:i, xmax:i, ymin:i, ymax:i)
graph:i = watchpoint(xmin:i, xmax:i, ymin:i, ymax:i, title:S)
```

`watchpoint` creates a plane on which each attached coordinate pair is drawn as
a moving point followed by a fading trail of about 100 ms. Neither axis is
time: the four limits fix the visible plane once, and the axes never rescale to
the incoming coordinates. The plot area is kept square so that a circular path
is drawn as a circle rather than an ellipse, and the grid uses eight divisions
per axis. A zero axis appears only when the corresponding range contains zero.

### `watchmeter`

```csound
graph:i = watchmeter(levels:k[], min_value:i, max_value:i, scale:i)
graph:i = watchmeter(levels:k[], min_value:i, max_value:i, scale:i, title:S)
```

`watchmeter` draws one bar per element of the array, labelled `ch1` through
`chN` under the horizontal axis, and feeds itself: there is no `watchadd` for a
meter. The array may hold from 1 through 32 elements and its length is the
channel count.

The levels are plotted as they arrive. Nothing is measured and nothing is
converted, so the array is expected to hold levels already, typically from `rms`
or `follow`; `scale` only declares their unit, `0` for linear gain or `2` for
decibels. Linear power is not accepted.

The range is fixed and never rescales. Above `max_value` the axis carries one
grid division of headroom, tinted like the peak marks, so a level going over is
seen going over instead of being pinned to the top edge; only past the headroom
is the value clamped.

The bar carries the level as received, without smoothing of its own: each packet
already holds the loudest value of one viewer frame. Only the peak mark holds,
for 1.5 s, before falling towards the bottom of the axis. All bars share one
color and the peak marks another, contrasting in both hue and lightness so that
a mark resting on its own bar is still visible. The grid is horizontal only.

### `watchtheme`

```csound
watchtheme graph:i, theme:i
```

Set `theme` to `0` for the default light palette or `1` for the dark palette.
Neutral graph components are inverted between themes. Signal curves use
dedicated high-contrast palettes: darker colors on the light background and
brighter colors on the dark background. Each of the 64 supported stream indices
receives a distinct color while preserving its base hue between themes. Meter
peak marks follow the same rule with a hue and a lightness of their own, so they
stay legible on the bar they rest on. Call
`watchtheme` at init time after creating the graph.

See the individual opcode pages in [`doc`](doc) for complete argument
descriptions and examples.

## Spectral scales

Watch transports spectral power. `watchspectrum` and `watchspectrogram` can
display it using one of three scales:

| Value | Scale | Conversion |
|---:|---|---|
| `0` | Linear gain | `sqrt(power)` |
| `1` | Linear power | `power` |
| `2` | Decibels | `10 * log10(power)` |

The default is linear gain. Automatic display ranges are:

- `0` through `1` for gain and power;
- `-120` through `0` for decibels.

Values below `-160 dB` are limited to the viewer's floor. The frequency axis is
currently linear.

## Complete example

The following example creates audio, control, spectrum, and spectrogram graphs:

```csound
<CsoundSynthesizer>
<CsOptions>
-odac
</CsOptions>
<CsInstruments>

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

Additional ready-to-run files are available in [`examples`](examples):

- [`watchscope.csd`](examples/watchscope.csd)
- [`watchcontrol.csd`](examples/watchcontrol.csd)
- [`watchspectrum.csd`](examples/watchspectrum.csd)
- [`watchspectrogram.csd`](examples/watchspectrogram.csd)
- [`watchtable.csd`](examples/watchtable.csd)
- [`watchpoint.csd`](examples/watchpoint.csd)
- [`watchmeter.csd`](examples/watchmeter.csd)
- [`watchadd.csd`](examples/watchadd.csd)

## Runtime requirements

A normal binary release requires:

- Csound 7;
- the Watch plugin binary;
- the matching `watch_viewer` executable.

The default release build statically links SDL3, SDL3_ttf, and FreeType into the
viewer and embeds its font. Users do not need to install those libraries
separately, and the Csound plugin itself does not link to SDL.

The project currently provides build and packaging support for:

- macOS x86-64 and Apple Silicon;
- Linux x86-64;
- Windows x86-64.

## Installation

### Risset

When Watch is available in the Risset index, install it with:

```sh
risset install watch
```

The Risset manifest treats the viewer as a platform-specific asset. The plugin
knows the standard Risset asset location and launches the installed executable
automatically.

### Prebuilt release

A release archive contains the plugin binary, `watch_viewer` (or
`watch_viewer.exe`), and the third-party license notices.

The simplest manual installation is to place the viewer next to the plugin.
Alternatively, install the viewer somewhere in `PATH` or set
`CSOUND_WATCH_VIEWER` to its complete path.

Csound's default user plugin directories used by this project are:

| Platform | Plugin directory |
|---|---|
| macOS | `~/Library/csound/7.0/plugins64` |
| Linux | `~/.local/lib/csound/7.0/plugins64` |
| Windows | `%LOCALAPPDATA%\csound\7.0\plugins64` |

## Building from source

### Build requirements

- CMake 3.16 or newer.
- A C11 compiler.
- Csound 7 plugin SDK headers, including `csdl.h`.
- Either the vendored SDL3/SDL3_ttf/FreeType sources or installed SDL3 and
  SDL3_ttf CMake packages.

The default configuration expects:

```text
third_party/SDL
third_party/SDL_ttf
third_party/SDL_ttf/external/freetype
third_party/font/AtkinsonHyperlegible-Regular.otf
```

### Default build

Configure and build the plugin and self-contained viewer:

```sh
cmake -S . -B build \
  -DBUILD_WATCH_OPCODES=ON \
  -DBUILD_WATCH_VIEWER=ON \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build --target watch watch_viewer -j4
```

The targets are named `watch` and `watch_viewer` with an underscore.

CMake searches common Csound installation paths. If it cannot find `csdl.h`,
provide the SDK include directory explicitly:

```sh
cmake -S . -B build \
  -DCSOUND_INCLUDE_DIR=/path/to/csound/include
```

When building against a raw Csound source checkout, `version.h` may live in a
generated directory. Add it with:

```sh
cmake -S . -B build \
  -DCSOUND_INCLUDE_DIR=/path/to/csound/include \
  -DCSOUND_EXTRA_INCLUDE_DIRS=/path/to/generated/headers
```

### Build with installed SDL

To use system packages instead of the vendored sources:

```sh
cmake -S . -B build \
  -DWATCH_USE_SYSTEM_SDL3=ON \
  -DBUILD_WATCH_OPCODES=ON \
  -DBUILD_WATCH_VIEWER=ON

cmake --build build --target watch watch_viewer -j4
```

This mode requires SDL3 3.4 or newer and SDL3_ttf 3.2 or newer. For distributable
release archives, use the default vendored build so the viewer remains
self-contained.

### Build only one component

Viewer only, without requiring Csound headers:

```sh
cmake -S . -B build \
  -DBUILD_WATCH_OPCODES=OFF \
  -DBUILD_WATCH_VIEWER=ON

cmake --build build --target watch_viewer -j4
```

Plugin only:

```sh
cmake -S . -B build \
  -DBUILD_WATCH_OPCODES=ON \
  -DBUILD_WATCH_VIEWER=OFF

cmake --build build --target watch -j4
```

The plugin-only build still requires a compatible viewer at runtime.

### Install

The CMake install step copies the plugin into the platform-specific user Csound
directory and installs the viewer under the selected prefix:

```sh
cmake --install build --prefix "$HOME/.local"
```

Make sure the prefix's `bin` directory is in `PATH`, or set
`CSOUND_WATCH_VIEWER` explicitly.

### Create a release package

With both components enabled, build the dependency-free archive used by GitHub
Releases and Risset:

```sh
cmake --build build --target watch_release_package
```

The archive is written under `build/dist` and contains:

- the platform-specific Csound plugin;
- the standalone viewer;
- SDL3, SDL3_ttf, FreeType, and font license notices.

## Running from the build directory

On macOS or Linux:

```sh
OPCODE7DIR64="$PWD/build" csound examples/watchscope.csd
```

In Windows PowerShell:

```powershell
$env:OPCODE7DIR64 = "$PWD\build"
csound examples\watchscope.csd
```

Because the viewer is next to the plugin in the build directory, it is found
automatically.

The viewer can also be launched independently:

```sh
./build/watch_viewer
```

Only one viewer can bind the local UDP endpoint at a time. A running viewer can
serve multiple graphs and multiple Csound processes.

## Tests

The [`utest`](utest) directory holds `.csd` files that exercise one area each.
They are run like any other orchestra and, where the checks are numeric, report
a summary through Csound's `--run-unit-tests` assertion opcodes:

```sh
OPCODE7DIR64="$PWD/build" csound utest/registry.csd
```

| File | Area |
|---|---|
| [`registry.csd`](utest/registry.csd) | Handle allocation and graph-registry capacity |
| [`scope_stream.csd`](utest/scope_stream.csd) | Audio streams at `ksmps = 1` and file playback |
| [`control.csd`](utest/control.csd) | Control packets, multiple streams, residual flush |
| [`control_ranges.csd`](utest/control_ranges.csd) | Asymmetric and strictly positive y ranges, `expseg` envelopes |
| [`spectral.csd`](utest/spectral.csd) | Spectrum and spectrogram graph creation |
| [`spectral_audio.csd`](utest/spectral_audio.csd) | Spectral streams fed by real audio |
| [`table_gens.csd`](utest/table_gens.csd) | GEN5, GEN7 and GEN8 tables with automatic and explicit ranges |
| [`point.csd`](utest/point.csd) | Plane aspect, trail, several points, asymmetric ranges |
| [`meter.csd`](utest/meter.csd) | Bar count, channel names, peak hold, clamping, 1 and 32 channels |
| [`ftable_transfer.csd`](utest/ftable_transfer.csd) | Deferred table transfer and graph cleanup |
| [`theme.csd`](utest/theme.csd) | Light and dark themes |

The graph tests are also visual: `control_ranges.csd`, `table_gens.csd`,
`point.csd` and `meter.csd` carry the expected result in each window title.

## Viewer discovery and lifecycle

When the first graph is created, the sender thread transmits its configuration
and gives an existing viewer 150 ms to acknowledge it. If no acknowledgement
arrives, the sender thread launches the viewer without blocking the audio
performance callback.

The executable is resolved in this order:

1. the path stored in `CSOUND_WATCH_VIEWER`;
2. an executable next to the plugin;
3. the standard Risset asset directory;
4. `watch_viewer` or `watch_viewer.exe` found through `PATH`.

Every `watchscope`, `watchcontrol`, `watchspectrum`, `watchspectrogram`,
`watchtable`, `watchpoint`, or `watchmeter` call creates a new graph window. The viewer requests that each new
window be raised and focused; the final decision remains subject to the desktop
window manager's focus-stealing policy. Windows are assigned grid positions in
the usable area of the display; positions are reused when there are more
windows than available grid cells. Windows may be closed manually.

A graph window outlives the note that created it and closes with the session, so
a table plotted by a short note can still be read afterwards. The one exception
is a graph replaced by a `reinit`: the opcode creates a new graph mid note, and
the previous window, which no longer stands for anything, is closed.

> [!NOTE]
> Reinitializing a graph opcode is handled but is not a good idea. Every pass
> replaces a window, and a window is an operating-system window with its own
> renderer and font textures: creating and destroying one is far more expensive
> than anything else the viewer does. At twenty reinitializations per second the
> viewer measures around 59% of a CPU core against about 2% in normal use, and
> the windows it is being asked to close start lagging behind the ones it is
> being asked to open. Create graphs once, outside reinit blocks, and let the
> signals change instead.

When a Csound session ends, it sends a session-close message three times. The
viewer closes only the windows belonging to that session. If no graphs remain,
it waits 500 ms before terminating; a new configuration received during that
interval cancels shutdown. Closing the last window manually terminates the
viewer immediately.

## Technical specifications and limits

| Property | Value |
|---|---|
| Csound API | 7 |
| Network transport | UDP over loopback |
| Viewer endpoint | `127.0.0.1:48120` |
| Protocol version | 6 |
| Maximum graphs per Csound instance | 32 |
| Maximum open graphs in one viewer | 32 across all Csound sessions |
| Maximum streams per graph | 64 concurrent; a slot is released when the attaching instance ends |
| Audio samples per packet | Up to 256 |
| Control samples per packet | Up to 256 |
| Point samples per packet | Up to 128 (two interleaved floats each) |
| Point publishing cadence | `ceil(kr / 60)` points, one viewer frame |
| Point trail length | Approximately 100 ms, derived from `kr` |
| Meter channels per graph | Up to 32, set by the length of the levels array |
| Meter publishing cadence | One frame per viewer frame: `ceil(kr / 60)` control cycles reduced to their loudest value |
| Meter ballistics | Bar unsmoothed; peak held 1.5 s, then 1.0 s release |
| Meter headroom | One grid division above the declared maximum |
| Meter display latency | Up to one packet window plus one viewer frame, about 35 ms |
| Spectral bins per packet | Up to 256 |
| Function-table samples per packet | Up to 256 |
| Maximum function-table size | 1,073,741,824 samples (`Csound MAXLEN`) |
| Per-stream sender queue | 50 bounded slots |
| Viewer refresh rate | 60 Hz |
| Scope render latency | Approximately 50 ms |
| Control batching latency | Up to approximately `256 / kr` seconds |
| Default window size | 600 × 400 |
| Minimum window size | 360 × 260 |
| Default axis/grid divisions | 10 horizontal, 8 vertical |
| Maximum axis/grid divisions | 256 per axis; unrelated to sample or FFT-bin counts |
| Maximum title length | 383 bytes plus terminator |
| Spectral dB floor | `-160 dB` |

Large audio blocks, FFT frames, and function tables are divided across multiple
UDP packets. Control values are accumulated into batches of 256 samples.
Function-table packets carry their sample offset and transfer size; the viewer
displays the plot only after the transfer is complete.

The viewer validates packet magic, protocol version, type, payload length,
graph/stream identifiers, sample rate, FFT metadata, ranges, and title
termination before accepting data.

## Troubleshooting

### `csdl.h` not found

Install the Csound 7 development headers or configure with:

```sh
cmake -S . -B build \
  -DCSOUND_INCLUDE_DIR=/path/to/csound/include
```

### `version.h` not found

When using a Csound source checkout, point CMake at the generated header:

```sh
cmake -S . -B build \
  -DCSOUND_INCLUDE_DIR=/path/to/csound/include \
  -DCSOUND_EXTRA_INCLUDE_DIRS=/directory/containing/version.h
```

### Csound reports that an opcode does not exist

Confirm that Csound is loading the build or installation directory:

```sh
OPCODE7DIR64="$PWD/build" csound examples/watchscope.csd
```

Also confirm that the plugin was built against the Csound 7 API and with the
same `MYFLT` precision as the running Csound installation. `USE_DOUBLE` is
enabled by default.

### The viewer cannot be launched

Keep the viewer next to the plugin or provide its absolute path:

```sh
CSOUND_WATCH_VIEWER="/absolute/path/to/watch_viewer" csound example.csd
```

On Windows:

```powershell
$env:CSOUND_WATCH_VIEWER = "C:\absolute\path\watch_viewer.exe"
csound example.csd
```

### UDP bind fails

Another viewer or process is probably using `127.0.0.1:48120`. Normally a single
viewer should be shared by all running Csound sessions. Stop the stale process
and launch the example again.

### Data is occasionally missing

The sender queues are intentionally bounded. If the viewer or sender cannot keep
up, visualization packets are dropped to protect real-time audio performance.
Watch is not designed as a signal recorder.

## Documentation

- [Overview and architecture](doc/index.md)
- [`watchscope`](doc/watchscope.md)
- [`watchcontrol`](doc/watchcontrol.md)
- [`watchspectrum`](doc/watchspectrum.md)
- [`watchspectrogram`](doc/watchspectrogram.md)
- [`watchtable`](doc/watchtable.md)
- [`watchpoint`](doc/watchpoint.md)
- [`watchmeter`](doc/watchmeter.md)
- [`watchtheme`](doc/watchtheme.md)
- [`watchadd`](doc/watchadd.md)

## Roadmap

- further rendering and labeling refinements while keeping the interface simple.

## Dependencies and licenses

The Watch plugin is distributed under the LGPL as declared in
[`risset.json`](risset.json).

The default viewer build includes:

- SDL3;
- SDL3_ttf;
- FreeType;
- Atkinson Hyperlegible.

Their license notices are included in generated release archives under
`licenses/`.

## Author

Pasquale Mainolfi, 2026
