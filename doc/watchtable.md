# watchtable

## Abstract

Take an init-time snapshot of a Csound function table and plot it after the
viewer has received the complete table.

## Description

`watchtable` creates one static graph without returning a graph handle. The
x-axis represents table indices and the y-axis represents table values.

The opcode copies the function table at init time. A dedicated sender thread
then divides the snapshot into packets after the viewer acknowledges the graph
configuration. Packets carry their destination offset and the total transfer
size, so the viewer can assemble them independently of packet boundaries. No
partial waveform is displayed: the plot becomes ready only when every sample
has arrived.

The snapshot is independent of later modifications to the original function
table. The protocol accepts up to Csound's maximum table length:
1,073,741,824 samples in the default double-precision build. Float or
`SHORT_TABLE_LENGTH` Csound builds impose their own lower `MAXLEN`.

## Syntax

```csound
watchtable table:i [, ymin:i [, ymax:i]]
watchtable.t table:i, ymin:i, ymax:i, theme:i
watchtable.s table:i, ymin:i, ymax:i, title:S
watchtable.ts table:i, ymin:i, ymax:i, theme:i, title:S
```

## Arguments

* `table:i`: number of an existing function table.
* `ymin:i` (optional): lower display limit. When omitted, the viewer derives it
  from the completed table.
* `ymax:i` (optional): upper display limit. When omitted, the viewer derives it
  from the completed table. When both limits are supplied, `ymax` must be
  greater than `ymin`.
* `theme:i`: `0` selects the light theme; `1` selects the dark theme.
* `title:S`: window title for the `.s` and `.ts` variants.

## Execution Time

* Init

## Example

The complete example is available as
[`examples/watchtable.csd`](../examples/watchtable.csd).

```csound
giwave ftgen 0, 0, 65536, 10, 1, 0.5, 0.25, 0.125

instr PlotTable
    watchtable.s giwave, -1, 1, "Function table"
endin
```

## See also

* [`watchscope`](watchscope.md)
* [watch overview](index.md)

## Credits

Pasquale Mainolfi, 2026
