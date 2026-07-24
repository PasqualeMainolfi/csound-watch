<CsoundSynthesizer>
<CsOptions>
-odac
</CsOptions>
<CsInstruments>
sr = 48000
ksmps = 32
nchnls = 2
0dbfs = 1

table@global:i = ftgen(0, 0, 1024, 10, 1, 0.5, 0.25)
watchtable(table, -1, 1, 0)
watchtable(table, -1, 1, 1, "Dark table")

instr ThemeGraphs
    light:i = watchscope(0.25, 10, 8, -1, 1, "Light theme")
    dark:i = watchscope(0.25, 10, 8, -1, 1, "Dark theme")

    watchtheme(light, 0)
    watchtheme(dark, 1)

    signal:a = oscili(0.8, 220)
    watchadd(light, signal)
    watchadd(dark, signal)
endin
</CsInstruments>
<CsScore>
i "ThemeGraphs" 0 10
</CsScore>
</CsoundSynthesizer>
