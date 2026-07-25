# watchtheme

## Abstract

Select the light or dark palette for an existing graph.

## Description

`watchtheme` changes the theme stored in a graph configuration. The light theme
is the default. Neutral components such as backgrounds, grid, axes, text, and
spectrogram intensity are inverted for the dark theme. Signal curves use
separate high-contrast palettes: darker colors on the light background and
brighter colors on the dark background.

Each stream in a line graph receives a color according to its stream index.
All 64 supported stream indices receive distinct colors. The light palette
maintains a contrast ratio of at least 5:1 against the plot background; the dark
palette maintains at least 7:1. Corresponding light and dark colors preserve the
same base hue.

The opcode retransmits the graph configuration when necessary, so it is safe to
call immediately after the graph-creation opcode.

## Syntax

```csound
watchtheme graph:i, theme:i
```

## Arguments

* `graph:i`: handle returned by `watchscope`, `watchcontrol`, `watchspectrum`,
  or `watchspectrogram`.
* `theme:i`: `0` selects the light theme; `1` selects the dark theme.

`watchtable` does not expose a graph handle. Its theme is selected directly
with the `watchtable.t` or `watchtable.ts` variant instead.

## Execution Time

* Init

## Example

```csound
graph:i = watchscope(0.5, 10, 8, -1, 1, "Dark scope")
watchtheme(graph, 1)

signal:a = oscili(0.8, 220)
watchadd(graph, signal)
```

## See also

* [`watchscope`](watchscope.md)
* [`watchcontrol`](watchcontrol.md)
* [`watchspectrum`](watchspectrum.md)
* [`watchspectrogram`](watchspectrogram.md)
* [watch overview](index.md)

## Credits

Pasquale Mainolfi, 2026
