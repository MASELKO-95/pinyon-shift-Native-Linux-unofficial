# Owned UI draw capture

RAY-00 partial result, 2026-09-25. The opt-in
`--pinyon_shift_snr01_trace_source_frame=6801` path now copies prepared UI
draw inputs before their observer callback returns and joins each draw to its
final GPU state by `IssueDraw` ordinal. It writes a bounded `RAYUI001` binary
artifact in the render-test output directory. Compatibility output remains
visible; this path is diagnostic and is not the live native feed.

The short `fh1-native-ui-admission-stress.fh1test` route stopped at frame
6805. RelWithDebInfo preview build and launch exited successfully. The local
artifact `.local/native-ui-owned2-20260925/ordered-ui-6801.bin` passed
`python tools/verify-ordered-ui-capture.py`:

| Source frame | UI draws | Shader pairs | Vertex bytes | Index bytes | Versioned texture bindings | File bytes |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 6801 | 165 | 3 | 65,710,368 | 118,080 | 58 | 67,396,688 |

The verifier found no missing final states or internal draw-sequence gaps and
checked every saved vertex/index snapshot against its recorded hash. The
first 64 MiB budget dropped two UI draws in an earlier run; the 128 MiB budget
captured this run without a rejection. UI draw count varies with the route's
timing; the artifact's complete flag checks the captured sequence, not the
total UI draws the guest intended to emit.

At this checkpoint, it was **not** an offline replay input yet. Texture
bindings had allocation
and payload-generation identities but no texture pixels. Clear, copy/resolve,
and world/car work are not in this artifact, and the known HUD-gap frame has
no UI producer at this seam. The next slice is a single ordered full-frame
record with those target events and immutable texture inputs, followed by an
offline D3D12 image. The repeated vertex snapshots are deliberately bounded
for diagnosis; live shadow capture needs resource reuse instead.

**Follow-up, 2026-09-25:** the capture now writes `RAYUI002`, preserving the
original fetch length while storing only vertices referenced by the draw's
index buffer. The verifier still reads `RAYUI001`. In a 166-draw replay run,
vertex snapshot payload fell from about 66.3 MB of declared fetch ranges
to 345,840 bytes, and the complete file was 2,045,836 bytes. The original
textured HUD still rendered in the saved image. This removes the largest
avoidable CPU copy in the diagnostic path, though live shadow still needs
a bounded multi-frame owner and source texture retention.
