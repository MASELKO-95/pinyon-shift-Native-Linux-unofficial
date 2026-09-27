# Bounded rolling ordered-event seam

The selected-frame ordered capture cannot by itself drive a continuous race.
An opt-in `--pinyon_shift_native_ordered_live_probe=true` now records draw,
copy and optimized-clear metadata for 64 source frames starting at
`--pinyon_shift_native_race_capture_start_frame`. It uses the existing
eight-frame in-memory ring and ordered sequence keys. The default-off probe
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
