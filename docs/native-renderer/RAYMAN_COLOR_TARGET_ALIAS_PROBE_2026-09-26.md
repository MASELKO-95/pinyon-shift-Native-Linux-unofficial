# Main-scene color alias probe — 2026-09-26

The selected output-5001 stream alternates `RB_COLOR_INFO[0]` values
`0x00030000` and `0x000C0000`. These are **not two EDRAM bases**. In the
SDK register definition, bits 0–10 are `color_base` and bits 16–19 are
`color_format`; both values use base zero. Format 3 is
`k_2_10_10_10_FLOAT`, and format 12 is
`k_2_10_10_10_FLOAT_AS_16_16_16_16`. The SDK maps both to the same
storage format and to `DXGI_FORMAT_R16G16B16A16_FLOAT` for its D3D12 render
target. A native replay must preserve their shared target contents while
handling format semantics. Splitting them into separate scene surfaces would
discard the overlap visible in the current frame.

Two temporary selected-frame probes issued only supported draws with one
value at a time. The `0xC0000` image retained the car, crowd and vegetation
but lost the signs, truck and near barrier. The `0x30000` image retained
those structures and ground but lost the car and most crowd. Both kept the
same-frame HUD. The merged native image contains both groups. The probes
used the RelWithDebInfo executable, the installed preview save and the
continuous race route; both exited normally. Images are under
`.local/ray-ui-native-promotion-20260925/color-c0000-probe-correct/` and
`color-30000-probe-correct/`. The temporary draw filter was removed.

A second temporary trial changed the native scene scratch and its world
pipelines from `R10G10B10A2_UNORM` to `R16G16B16A16_FLOAT`, leaving the
final guest output format intact. Its selected frame and full route exited
normally; `verify-ordered-frame.py --require-owned-inputs` passed with
2,876 owned draws and 413 pinned texture versions. The saved frame still
had flat ground and the neon-yellow structure. A simple bright-yellow pixel
census found 31,814 pixels in the earlier UNORM capture and 32,132 in the
float trial, with gameplay timing differing between runs. This does not
establish a visual gain or a matched quality comparison. The trial was
removed and the RelWithDebInfo preview rebuilt from the restored source.

The next replay step remains the ordered target-version and copy chain,
especially the 1280×720 texture at guest base `497831936` used before and
during the three main scene tiles. Preserve the `0x3`/`0xC` alias when
implementing it. Float scratch should be revisited only alongside the
corresponding resolve/output conversion and a matched comparison.
