# Race UI producer cadence — 2026-09-25

A bounded draw census around source frame 6803 counted draws to the actual
1280-wide UI target (`RB_SURFACE_INFO=0x14000500`, color 0 `0x000A0000`).
The saved race route exited normally with native presentation disabled.

| Source frame | UI-target draws |
| ---: | ---: |
| 6795 | 170 |
| 6796 | 0 |
| 6797 | 170 |
| 6798 | 170 |
| 6799 | 170 |
| 6800 | 0 |
| 6801 | 170 |
| 6802 | 0 |
| 6803 | 170 |
| 6804 | 170 |

Evidence: `.local/ray-ui-adjacent-census-20260925/` and the bounded
`FH1 RAY00 adjacent` lines in the AppData preview runtime log. The
compatibility screenshots from two earlier runs still display a readable
HUD when source frame 6803 contains zero UI-target draws. Thus a complete
frame stream must represent retained UI state across a no-producer frame;
requiring current-frame UI draws would reject otherwise usable output.
The exact render-target/resolve dependency carrying that retained state is
not yet identified, so replay must keep whole-frame fallback there.

The same diagnostic's 166-draw frame verifies as `RAYUI002` at
`.local/ray-ui-trimmed-capture-20260925/ordered-ui-6803.bin`.
It stores 345,840 vertex bytes out of 66,259,352 bytes of guest fetch ranges;
the complete file is 2,045,836 bytes. Its saved native image retains all
textured HUD elements. The next live-shadow feed can use this bounded input
size, but must track the last valid UI version and source textures across
producer gaps rather than inventing current-frame UI draws.
