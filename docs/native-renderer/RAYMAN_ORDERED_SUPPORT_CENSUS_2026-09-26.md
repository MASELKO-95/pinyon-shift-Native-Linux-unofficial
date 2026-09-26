# Ordered native support census — 2026-09-26

The continuous race route reached selected output frame 5001 and wrote both
`ordered-frame-5001.csv` and `ordered-native-support-5001.csv`. The latter
records the existing native draw family for each ordered draw, or `-1` when
the current native scene snapshot cannot issue it. The two files join by
command ordinal, so the census distinguishes omitted target work from draws
already visible with an approximate material.

The strict verifier passed: 3,091 events, including 2,972 draws, 98 copies,
and 21 clears; all draws had owned geometry and final state, and all 416
referenced texture versions were pinned. Exactly 1,595 draws joined a native
family and 1,377 did not. The continuous render test exited normally.

| Phase in ordered stream | Draws outside current native families | Detail |
| --- | ---: | --- |
| Before the main scene | 1,049 | 578 have no pixel shader; includes 521 draws to a 1280×720 depth-only surface and 205 draws to a smaller color/depth surface. |
| Main tiled scene | 63 | 24 have no pixel shader; the other 39 span several pixel programs. The two main surface configurations contain 1,658 draws overall. |
| After the main scene | 265 | Includes all 166 HUD draws, which the current pilot renders in its separate same-frame UI pass. |

The 602 unsupported draws without a pixel shader cannot be dismissed as
invisible: depth can feed later tests or resolves. Likewise, the separately
rendered HUD is present in the event stream but does not yet execute inside
its event loop. The first successful 1280×720 copy targets guest base
`497831936` before the main scene. Its pinned 1280×720 texture version
(`allocation_id=74`, generation `7907`) is read by 11 following offscreen
draws and two draws in each of the three main-scene tiles. The first tile
copy overwrites that base; the next two write adjacent offsets, and the
main-scene consumers use generations `7908` and `7909` respectively. This
is a concrete target-version dependency. A later
[copy-input capture](RAYMAN_ORDERED_COPY_INPUTS_2026-09-26.md) confirms the
same destination and tile pattern selects a **depth** source. The selected
native path currently
executes one main clear and uses the terminal full-size copy as its
presentation guard; it does not reconstruct the intervening target contents.

This redirects the next slice away from another pixel-material trial. First
replay this intermediate target and its version updates in the existing
D3D12 output, using the pinned guest versions as a bring-up comparison.
Then issue the 39 missing color draws on the main surface if their
visible effect remains; integrate the same-frame HUD at its event ordinals.
Do not treat the raw 1,377 count as 1,377 equally visible gaps.

Reproduce from the RelWithDebInfo preview, installed preview state, and
`continuous-race.fh1test` with the selected-frame flag
`--pinyon_shift_snr01_trace_source_frame=5001`. The capture and saved native
images are under
`.local/ray-ui-native-promotion-20260925/native-support-continuous-output/`.
Run `python tools/verify-ordered-frame.py ordered-frame-5001.csv
--require-owned-inputs` from that directory to check the ordered stream and
its support manifest together.
