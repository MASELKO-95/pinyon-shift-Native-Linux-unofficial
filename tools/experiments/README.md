# Non-renderer experiment patches

These patches preserve diagnostic edits separately from renderer work that was
already dirty when the experiment branch began. Check the target working tree
before applying them; the I/O and wait patches were still applied locally at
the September 10 consolidation.

The build-only change in `non-renderer-registration.patch` is already on `dev`
in `cmake/PinyonShiftRexGlue.cmake`; do not apply it again there. It
applies independently at the project root against starting commit `f4c39de`;
both forward application against that file and reverse application against the
tested current file were checked. It excludes the unused main registration
translation unit while retaining the image mapping table and facade entries.
The remaining patches below are optional diagnostic infrastructure.

- `non-renderer-tooling.patch` applies at the project root against the three
  original files in the private `baseline-20260909T050809Z/project/files`
  snapshot. It adds isolated build selection, performance-only recording,
  process I/O counters and extra diagnostic arguments to the existing tools.
  The discovery tools were originally untracked, so this is a dependent patch,
  not a standalone change against project HEAD.
- `non-renderer-io-profile.patch` applies inside `thirdparty/shiftglue-sdk`
  against SDK commit `6db74f6de0230727358d93f8a221f40fbba6a792`. It adds a
  default-off synchronous-read duration diagnostic. It changes no I/O policy.
- `non-renderer-wait-profile.patch` applies in the SDK at the same revision.
  It adds default-off, per-thread aggregation of completed guest-object waits.
  It changes no wait result/timeout policy; pending waits and partial buckets
  at hard exit are omitted. Profiling overhead still prevents treating its
  measurements as an uninstrumented performance comparison.

All passed `git apply --reverse --check` against the tested working files.
Preserved line endings matter for the tooling patch. Before integrating either,
apply it to the corresponding prerequisite revision and run the recorder's
`--self-test`, an isolated launch, and the read-profiling on/off checks as
applicable. Do not stage unrelated SDK or renderer changes with these patches.

Measurements, sessions, caveats and merge decisions are recorded in
[the results ledger (historical)](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/NON_RENDERER_OPTIMIZATION_RESULTS.md).

Current decisions: [non-renderer findings](../../docs/DEVELOPMENT.md#non-renderer-findings).
