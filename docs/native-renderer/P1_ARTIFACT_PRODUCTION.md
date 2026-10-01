# P1: clean artifact production

2026-09-07: in progress. P0 is deferred at the user's request.

Since then, graphics preparation (`tools/prepare-fh1-shaders.ps1`, run by
`launch-preview.ps1`) runs this production with the
`fh1-shader-preparation` route for the selected scale, keeps a receipt of
its inputs and activates the validated set; recorded pack misses make it
run again (see [pack misses](SHADER_PACK_FORMAT.md#pack-misses-and-self-repair)).
The results below were measured with the Xenos renderer, before its removal;
packs are now produced and validated with the native renderer.

After verified extraction and `build-preview.ps1`, run:

```powershell
.\tools\provision-toolchain.ps1
.\tools\produce-fh1-artifacts.ps1 `
    -WorkRoot .local/native-renderer/production-check `
    -RenderTestScript config/render-tests/fh1-artifact-startup.fh1test `
    -Scale 1 -JsonEvents
```

Choose a new work directory under the repository's `.local` for each run.
The command builds the explicit producer and archive helper, extracts the disc
shader corpus, collects shader/pipeline artifacts in empty state, verifies and
packages them, then runs the same script in separate empty state with the
compiler-free renderer. It does not copy player saves or use developer caches.
The adapter and translation configuration come from the running producer.
Artifacts and diagnostic logs stay in the work directory; normal launch state
is not modified. The producer DLL is removed from the launch directory after
production. Interrupted work remains available for diagnosis; resume/reuse is
not implemented yet.

`production.json` records the pack configuration/hash, binary hashes, corpus
and script hashes, process IDs and execution counters. `route-validated` means
only the supplied route completed without shader-pack misses, runtime shader
translation, synchronous pipeline creation, or missing prewarmed pipelines.
It does not establish image parity, full gameplay coverage, or driver-cache
cold-start performance. `gameplay_ready` deliberately remains false.
`-AllowShaderMisses`, which graphics setup passes, turns route pack misses
into a warning recorded under `shader_misses`; a run with misses is never
`route-validated` (see [pack misses](SHADER_PACK_FORMAT.md#pack-misses-and-self-repair)).

## Verified results

- NVIDIA RTX 4080, driver 32.0.15.8108, 1x: two independent extractions/producer
  runs from empty shader state generated the same 463,645,472-byte pack:
  `AE3C2457FD31C306900A324C0AE9D00A491D05333E25C153EEAE58440053C80F`.
- 4,293 containers yielded 11,726 corpus programs. Initial production translated
  9,600 vertex and 12,106 pixel variants with zero reported translation failures;
  startup added variants for a final pack of 21,735 entries.
- Both compiler-free startup runs loaded 11,365 analysis records and prewarmed
  all 23 captured pipelines. Both completed with zero runtime translations,
  synchronous pipeline creations and unprewarmed pipeline draws. Some draw/copy
  execution keys differed between runs (39/40 unrecorded executions); this is
  not a zero-fallback or full-renderer coverage claim.
- Shipping renderer stayed at SHA-256
  `75521DA21DBAC16D95CA6C6F9E11640A5A601A393E86C6D4192044782A3863E4`.
- The packaged CMake project configured with the SDK outside the payload and
  explicitly supplied existing generated CPU sources. This checks source/layout
  completeness, not a fresh disc-to-playable installation or a full rebuild.
- 593 tooling tests passed using the provisioned local Python runtime.

Local evidence: `.local/native-renderer/p1-clean/` and
`.local/native-renderer/p1-command/production.json`.

## Fixes retained

- Provision the hash-pinned [Python 3.13.15 embeddable runtime](https://www.python.org/downloads/release/python-31315/)
  and use its explicit path for launcher shader-pack staging. No system Python,
  PATH changes, pip packages, or separate installation are needed.
- Stop launch when the pack staging process fails; place producer DLL staging
  inside the launch cleanup scope.
- Include CMake-referenced native test/helper sources and offline artifact tools
  in the launcher source payload. Resolve SDK paths through `REXSDK_DIR`, which
  also works with the launcher's fallback SDK location.

## Remaining P1 gates

1. Close the clean-production coverage gap before marking setup ready. The
   developer pack has 22,012 variants; the fresh pack has 21,735. In particular,
   known car-selection vertex variants C41DD15CBD361350/01FF,
   CE81AE65F9C5A57B/007F and D60688109AC80358/003F are absent. The current producer
   only specializes these if their programs were already loaded; zero disc
   translation failures does not mean complete gameplay coverage.
2. Integrate production into setup and on-demand scale changes only after that
   gap is closed. Current setup still marks the compiled executable ready;
   it does not invoke this qualification command automatically.
3. Add receipts that validate every input/artifact before reuse, atomic activation
   of complete sets, cancellation/resume, and exact-device warmup invalidation.
4. Qualify fresh-player gameplay, race, map, pause, photo, FMV and scales 1x/2x/3x.
   The startup script above is intentionally a narrow initial smoke check.
5. Run the matrix on AMD and Intel hardware and a clean installed launcher.
   No AMD/Intel device is available on this machine; neither vendor is qualified.
