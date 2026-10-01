# FH1 native shader capture

The capture seam records valid ReXGlue D3D12 translations into the V2 runtime
pack manifest. It is local-only diagnostic tooling: capture itself does not
change pipeline state, draw submission, resolve behavior, or presentation.

## Captured data

Each callback supplies:

- vertex or pixel stage;
- 64-bit guest shader hash and exact 64-bit modification key;
- translator version, GPU vendor, D3D12 flags, and render scale;
- native D3D container bytecode;
- translated texture bindings, sampler bindings, and used-texture mask.

Invalid translations never reach the callback. Cached pack hits are omitted,
which makes a capture taken while a pack is loaded an exact runtime-miss set.

Capture is enabled only by an absolute
`PINYON_SHIFT_NATIVE_SHADER_CAPTURE_DIR` containing a `.local` component. The
writer permits at most 65,535 identities, 16 MiB per container, and 512 MiB
total. It writes through temporary files, verifies stable configuration and
binding masks, and emits `pinyon-shift.native-shader-pack.v2`.

## Efficient stored-corpus workflow

FH1's existing `.xsh` file contains guest shader programs and `.xpso` contains
their observed pipeline specialization keys. They can drive all known
translations during startup without replaying a long race:

```powershell
python .\tools\run-fh1-render-test.py `
  .\config\render-tests\fh1-smoke.fh1test `
  --state-root <preview-state> `
  --seed-shader-storage `
  --shader-capture-dir .\.local\native-renderer\aot-capture `
  --output .\.local\native-renderer\aot-capture-run `
  --record-baseline
```

Only `4D5309C9.xsh` and `4D5309C9.rtv.d3d12.xpso` are copied into the isolated
state. The pipeline allowlist is intentionally not copied, so every stored FH1
specialization is translated. Saves and the original cache are untouched.

Build the resulting pack with `tools/native-shader-pack.py`. Then validate a
representative path with both independent gates:

```powershell
python .\tools\run-fh1-render-test.py `
  .\config\render-tests\fh1-hfr-modes-unlocked.fh1test `
  --state-root <preview-state> `
  --seed-pipeline-prewarm `
  --shader-pack .\.local\native-renderer\aot-capture\fh1.pnsp `
  --shader-capture-dir .\.local\native-renderer\misses `
  --require-zero-shader-misses `
  --output .\.local\native-renderer\strict-run `
  --record-baseline
```

`--seed-pipeline-prewarm` copies only the verified FH1 V3 pipeline allowlist,
so startup pipelines are prewarmed while the validation state stays isolated.

Locally derived bytecode, manifests, and packs must remain under `.local` and
must not enter support bundles or the public repository.

## Retail-disc corpus

The stored corpus is a qualification bridge, not the final producer: it can
only contain shaders encountered by an earlier run. FH1 instead exposes its
finite shader programs in `media/shaders/**/*.fxobj`. Extract them directly:

```powershell
python .\tools\extract-fh1-shader-corpus.py <game-root> `
  --output .\.local\native-renderer\fh1-disc-shader-corpus\manifest.json `
  --binary-dir .\.local\native-renderer\fh1-disc-shader-corpus\ucode `
  --archive-extractor .\out\build\win-amd64-release\pinyon_shift_fh1_archive_extract.exe
```

The helper reuses ReXGlue's LZX decoder for FH1's method-21 `bin.zip` archives;
all 197 archived shader members were checked byte-for-byte against an
independent QuickBMS extraction. Missing the helper is an error rather than
silently producing an incomplete corpus.

For the supported retail disc this finds 4,293 containers and 3,349 unique raw
programs: 1,615 vertex and 1,734 pixel. The 34 vertex declarations embedded in
the same assets deterministically patch the retail fetch templates into 8,377
additional vertex programs, for 11,726 source programs total. The patcher
reproduces 275 of the 277 accumulated runtime vertex shaders that have an
identifiable retail parent; the two exceptions are FH1 DriverHands skinning
variants. Generated/system shaders and those two variants remain in the finite
title seed.

Stock XenosRecomp was tested against the first FH1 car-material asset. Its
scanner recognizes the containers, but its Sonic Unleashed-specific shader ABI
fails compilation on FH1 cube and sampler declarations. The usable reference
is therefore its deterministic asset-scanning model. Offline FH1 production
continues to use ShiftGlue's already-qualified translator. Automated session
`20260904T055222Z-p28564` produced 9,600 vertex and 12,098 pixel
specializations with zero failures. Merging those misses with the finite title
seed produced a 21,984-entry 2x pack; strict session
`20260904T055452Z-p33360` completed with zero runtime translations and no
shader-storage seed.

The same producer is qualified at every supported integer scale. Strict
no-seed sessions `20260904T060200Z-p47700` (1x),
`20260904T055452Z-p33360` (2x), and `20260904T060433Z-p11356` (3x) each
captured zero translation misses. Ordinary local launches now verify and stage
the matching scale pack automatically and enable the strict runtime gate.

The producer no longer needs an existing pack to discover FH1 specialization
masks. They are a fixed title-specific set, including five car-shader pairings
whose live interpolator count is lower than the asset declaration. Clean 1x
session `20260904T061601Z-p48584` translated the complete 9,600 vertex and
12,098 pixel asset set with zero failures and no input pack or `.xsh` storage.
The remaining finite seed consists only of runtime-generated/system programs
not present in retail `.fxobj` assets.

For an installation with a legacy `4D5309C9.xsh` and D3D12 `.xpso` cache,
automatic preparation also seeds those cache files into its isolated producer
state. This covers shader programs encountered by that installation, including
the saved Recaro Rush race, without copying its save or requiring runtime
translation. The cache hashes are part of the preparation key. A clean
installation still uses the retail-disc corpus and finite title seed.
