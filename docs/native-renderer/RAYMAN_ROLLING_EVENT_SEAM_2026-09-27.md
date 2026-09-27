# Bounded rolling ordered-event seam

The selected-frame ordered capture cannot by itself drive a continuous race.
An opt-in `--pinyon_shift_native_ordered_live_probe=true` now records draw,
copy and optimized-clear metadata for 64 source frames starting at
`--pinyon_shift_native_race_capture_start_frame`. It uses the existing
eight-frame in-memory ring and ordered sequence keys. The default-off path
does not change native presentation or guest rendering.

The first run captured draws and copies but zero clears. The D3D12 command
processor reported optimized clears only when the single trace-frame flag
matched; the bounded live gate now reports them too. On the installed-save
race-profile route, the post-fix run exited normally and logged:

| Source frame | Draw events | Copy events | Clear events | Copies to `484626432` |
| ---: | ---: | ---: | ---: | ---: |
| 5000 | 2912 | 95 | 20 | 2 |
| 5010 | 3311 | 98 | 21 | 2 |
| 5020 | 2939 | 94 | 20 | 2 |
| 5030 | 3255 | 94 | 20 | 2 |

The event mix agrees with the selected-frame census in scale, and the two
copies to the initial color destination recur across moving frames. This
removes the lack of rolling draw/copy/clear order as a blocker. It does not
yet make the 18 selected-frame producer draws or their exact shader, geometry
and texture inputs available in every live frame, nor execute target aliases
and resolves for those consumers. The selected-frame path remains a fixture.

The probe is diagnostic because capturing all draw metadata costs CPU. The
short trace-free windows measured 99.48 and 104.43 ms medians with the probe
on separate runs, against a prior 93.15 ms trace-enabled baseline; these
runs do not isolate a repeatable overhead. Leave it opt-in until the live
renderer consumes a smaller necessary slice. Ignored outputs are under
`.local/ray-rolling-ordered-probe-20260927/` and
`.local/ray-rolling-ordered-clear-20260927/`.

## Bounded producer replay check

The same 64-frame gate now captures full inputs for the 18 producers used by
the selected-frame ordered path: 12 initial-color draws, three main-scene
draws, and three small-target reductions. Four sampled moving frames each
reported 18 ready producers. The SDK also retains the initial color/depth
snapshot for those frames; previously it retained that resource only for a
single selected trace frame.

On the installed-save race-profile route with native promotion disabled,
ordered replay issued all 18 original draws and two initial-color copies for
15 consecutive shadow frames (source 5008–5022). The UI stayed readable and
the process exited normally. Source 5023 rejected at
`track_structure_texture`, an existing missing-material boundary on this
shadow route. The image at `native-shadow-5016.ppm` still has a flat blue sky,
unlit scenery, and a dark car compared with the same-run compatibility image;
ordered producer execution has **not** closed the visible world gap. Evidence
is under `.local/ray-rolling-ordered-shadow-v3-20260927/`.

A separate opt-in visible-output run exited normally, but its ordered path
rejected every sampled frame at `ordered_small_copy`: the reduction-copy
topology did not match the selected-frame replay contract. Whole-frame
fallback remained intact. Until that target chain is proven on moving final
frames, the rolling executor is restricted to shadow mode; enabling the
metadata probe alongside visible native output does not replace the existing
live scene path. The run is under `.local/ray-rolling-ordered-live-20260927/`.
After that gate, the same visible-output route promoted 29 consecutive
sampled frames (outputs 5001–5029) with no ordered-copy rejection and exited
normally; see `.local/ray-rolling-probe-native-fallback-20260927/`.

Two further diagnostic runs temporarily removed that gate. One promoted
ordered output through source frame 5030; another completed the existing
native/compatibility/native hot-toggle route, promoting both native segments
and returning cleanly from compatibility. This contrasts with the earlier
all-frame `ordered_small_copy` rejection, so the exact copy contract varies
across otherwise similar runs. The temporary change was removed. The
same-session captures `native-on.ppm`, `compatibility-off.ppm`, and
`native-on-again.ppm` show that the ordered path still has a flat sky,
unlit road/buildings, and dark car despite readable HUD. Evidence is under
`.local/ray-ordered-small-copy-diagnostic-20260927/` and
`.local/ray-rolling-ordered-toggle-diagnostic-20260927/`. Keep the
shadow-only gate until a bounded target/resolve correction produces a
clear final-image gain across moving frames.

Keep this replay opt-in while testing the scene color/resolve mismatch at a
single visible region. Do not expand the producer list or promote this ordered
path by default until a same-run final-frame comparison improves across moving
frames. The next check is an interactive steering drive; the scripted route
spends too long against the barrier to establish playability.
