# Late-race facade neon

The native UI admission stress route exposed an untested later view: across
output frames 6790–6810, the native scene contained 28,171–29,921 exact
RGB `(255,255,0)` pixels in the trackside structure. All eleven frames had
distinct images and readable race HUD, so this was a persistent material
artifact rather than a stale or missing frame. The earlier short race view
had no such pixels.

A selected source-6801 ordered capture passed
`verify-ordered-frame.py --require-owned-inputs` (3,302 events, 3,188 draws,
414 pinned texture versions, no missing final states or failed copies).
Its five-texture `0CBC533419F61E0D` / `56D45C45966FD938` facade family was
present in the main scene. A reversible run replacing only that original
pixel program with its flat fallback reduced the exact yellow count to zero
in all eleven stress frames. This isolates the neon to that shader path;
the precise bad input or translation remains unknown.

The retained bounded approximation samples this family's captured fetch-5
texture with the existing textured pixel path. The SDK already snapshots
that exact source-frame input. An eleven-frame live stress run and a separate
five-frame shadow/guest pair at 6801–6805 both had zero exact yellow pixels,
normal route exits, readable HUD and visible structure texture. Another
paired early race segment at 5040–5044 also exited normally without neon.
`fh1-native-race-hot-toggle.fh1test` passed its on/off/on verifier on the
retained build. The mode-boundary route also exited normally and passed
`verify-native-race-mode-boundary.py`: race native, pause, free roam and title
compatible.

The texture approximation is less detailed than a correct five-texture
program; some distant structural surfaces still look gray and repetitive.
Revisit the original program only after a paired late-race frame identifies
the differing texture view, constant or sampler. Do not restore it solely
because the short race view looks acceptable.
