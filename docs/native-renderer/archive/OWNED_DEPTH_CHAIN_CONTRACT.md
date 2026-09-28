# Owned depth-clear chain contract

Historical: describes the Xenos-era renderer's owned depth clear, removed with the Xenos renderer in `6b75238`; kept for reference.

Status: retained at symmetric 1x draw resolution; A1–A6 complete for that scope.
2x uses compatibility clears after failing performance retention. See
[A6 retention](A6_OWNED_DEPTH_RETENTION.md) and
[measurements and capture evidence (historical)](https://github.com/arcanite24/pinyon-shift/blob/53f9bf91b470f37cf7efb21c64dc1f8cce50c4c5/docs/native-renderer/P2_DEPENDENCY_RANKING.md).

## Boundary and identity

The selected chain is the FH1 depth-only rectangle clear that otherwise
materializes a 4x D24S8 target from the authoritative 1x D24S8 target, clears
depth, and subsequently transfers its contents back. Captures identify the
dominant target as base tile 720, pitch 13. Admission is based on validated
state and ownership, not these captured constants.

`D3D12CommandProcessor::IssueDraw` attempts the replacement before render-target
`Update`. It requires the known clear vertex shader, validated clear pipeline,
non-indexed rectangle geometry, depth writes, no pixel shader, color writes,
stencil writes, memory export, alpha-to-mask or active supported query path.
Vertex bytes are obtained through the existing CPU snapshot facility. Unsupported
state falls through to normal draw processing.

The identified game GPU producer is vertex program `1E6883FCCDE1F688` executing
a depth-only rectangle list with no pixel program. This is distinct from the
renderer-generated transfer shaders: the original 4x intermediate's full-frame
usage audit found transfer draws and clears, but no guest geometry draw. The
early draw hook is the first verified usable boundary with the required registers,
validated clear pipeline and readable vertex data. The upstream CPU routine that
emits these packets is not identified; bypassing that routine belongs to B3 and
is not performed by this implementation.

`ClearFh1OwnedDepth` looks up the existing 1x target by base, pitch, depth format
and sample count. Every EDRAM ownership range must currently name that target.
It validates all mapped rectangles and actual resource bounds before mutation.
Production admission requires symmetric 1x resolution and non-wrapping mappings.
The mapping helper was checked at 1x/2x; 2x correctness passed but its performance
failed retention, so the production draw guard excludes it.

The existing cache allocation supplies the resource generation: the candidate
does not keep a key-to-pointer mirror or reuse a pointer across calls. It resolves
the current allocation on every admitted clear. The same-key recreation check
verifies that empty ownership rejects admission and renewed ownership selects the
new allocation rather than cached contents from the previous lifetime.

## Lifetime and transitions

| Boundary | Current behavior and evidence |
| --- | --- |
| Creation | Reuses the existing cache resource; the candidate allocates no replacement target or persistent pointer. Missing ownership/resource rejects admission. |
| Clear | Queues a depth-write transition and submits barriers before depth-only `ClearDepthStencilView`. All rectangles are validated first. |
| Partial regions / stencil | Leaves stencil and exterior pixels intact. Captured 1x/2x before/after buffers pass exact checks; a separate 2x nonzero-stencil fixture also passes. |
| Current ownership | Leaves the ownership map on the 1x resource, so later consumers obtain the modified authoritative contents through existing cache lookup. |
| Floating-depth history | Leaves independent history owners intact. Captured 1x/2x mixed-history consumers pass exact per-sample depth and stencil reconstruction checks. |
| Alias / subsequent writer | Existing `Update` and ownership-transfer machinery still handles conflicting target layouts. This machinery has not been retired. Captured consumers cover the observed layouts, not every possible alias. |
| Resolve publication | Existing `Resolve` calls `DumpRenderTargets`, dispatches the resolve-copy shader, marks destination UAV writes pending, invalidates resolved texture ranges and reports written address/length. Exact dump-buffer checks pass at 1x/2x. `tools/check-fh1-owned-resolve.py` independently checks all eight captured final copies, including untouched bytes within exported destination ranges: zero mismatches at both scales. This does not cover every resolve format or writes outside those exported ranges. |
| Eviction | Common `ClearCache` retains current owners and both independent depth-history owners. Candidate holds no reference across calls. Diagnostic 1x/2x race runs each request two real GPU cache evictions after 1024/2048 native clears; completion ordering, subsequent native clears, captures and scoped timing checks all pass. |
| Destruction / reset | `DestroyAllRenderTargets` clears ownership before deleting resources; non-shutdown reset installs empty ownership. Subsequent admission must find a live owning resource again. `tools/check-fh1-owned-lifetime.py` compiles the actual lookup/eviction/destruction methods with a minimal resource shell and passes same-key recreation, null/missing/mixed ownership and history-retention checks. GPU device-loss/reset behavior remains unproven; supported live eviction is tested separately. |

Source anchors: `thirdparty/shiftglue-sdk/src/graphics/d3d12/command_processor.cpp`
(`IssueDraw`), `src/graphics/d3d12/render_target_cache.cpp`
(`ClearFh1OwnedDepth`, `Resolve`), and
`src/graphics/pipeline/render_target/cache.cpp`
(`GetFullyOwnedRenderTarget`, `ClearCache`, `DestroyAllRenderTargets`). The last
two paths are relative to the same SDK directory.

## Removed work and remaining requirements

### Cost coverage for A1

GPU milliseconds per sampled frame, feature off → on. Ordinary-transfer and
inclusive-phase rows come from separate same-binary diagnostic comparisons of
the same scripted scene at each scale. They are component attribution, not one
simultaneously measured additive frame budget.

| Chain component | 1x | 2x (not retained) | Interpretation |
| --- | --- | --- | --- |
| Initialize current owner | 0.084992 → 0.080794 | 0.321331 → 0.313065 | Complete retained transfer contract, once/frame |
| Clear draw, inclusive | 0.389717 → 0.004352 | 1.572267 → 0.010496 | Four/frame; off includes forward materialization |
| All eliminated round trips | 0.619241 → 0 | 2.577818 → 0 | Includes the forward work already inside the preceding row; do not sum them |
| Dump and resolve publication, inclusive | 0.027648 → 0.027221 | 0.076971 → 0.073557 | Four/frame; includes its barriers and copy/clear work |
| Mixed floating-depth consumer | 0.108358 → 0.107622 | 0.421581 → 0.440134 | Complete retained mapping, once/frame |

Clear preparation/recording wall time is 0.020817 → 0.006975 ms at 1x and
0.018258 → 0.004142 ms at 2x. Resolve recording wall time is
0.030708 → 0.029658 ms and 0.039875 → 0.026767 ms respectively. These are wall
durations inside the selected hook boundaries, not thread CPU attribution for
the entire game. Common processing before those boundaries remains part of the
whole-run CPU measurements, not a claimed removed cost.

All four accepted phase runs have 12 sampled frames, four calls per phase per
frame, zero reported query losses and no observed competing compiler processes.
The earlier contaminated pair is rejected. Full transfer-list audits validate
their own selected frames and zero losses, and compare retained mappings per tile
including independent depth history. The different range splits in the 1x off
run are combined only after proving identical full mappings.

Reproduction: local `p2/summarize-owned-contract.py` uses the repository's
`tools/rank-fh1-transfer-contracts.py`; `p2/compare-owned-contracts.py` checks
elimination and retained boundaries; `p2/summarize-owned-phases.py` checks phase
coverage and contention. Input/output directories are
`p2/owned-contract-{1x,2x}-{off,on}`, `p2/owned-phase-1x-{off,on}` and
`p2/owned-phase-2x-clean-{off,on}`, relative to `.local/native-renderer`.

### Consumer and lifetime coverage for A2

The captured native owner has 18 unique read events: four EDRAM dumps, eight
stencil-transfer draws and six mixed-depth-transfer draws. All are accounted for;
the four following resolve copies are checked independently through their final
destination ranges. Original-intermediate and candidate-owner full-frame usage
audits, rather than the shader label alone, establish the observed lifetime.
Exact clear, history, dump and publication checks cover contents; source inspection
and live eviction plus same-key recreation checks cover cache lifetime. Query and
memory-export paths are excluded from admission and retain normal processing.
Other scenes and device-loss behavior remain explicit qualification limits.

The captured candidate frame has no base-720/pitch-13 4x D24S8 texture and performs
four clears directly on the 1x owner. Diagnostic ordinary-transfer intervals fall.
This demonstrates removal of the intermediate resource and associated work in
the captured chain; it does not establish removal of all Xenos resource handling.
Same-binary diagnostic off/on contract audits at both scales also show no remaining
ordinary transfer referencing that intermediate. Removed round-trip intervals cost
approximately 0.619 ms at 1x and 2.578 ms at 2x per sampled off frame. Both retained
transfer boundaries match per tile and remain once per sampled frame. These
interval costs exclude clears, dumps, publication, CPU preparation and synchronization;
they are not measured frame-time improvements.
Translated pipeline validation, guest draw decoding, ownership transfer,
mixed-depth reconstruction, EDRAM dump, resolve copy and synchronization remain.

Repeated 1x runs show modest frame-time improvements; 2x frame-time results vary
by run block while CPU and private-memory reductions repeat. Single race-start
runs pass scoped motion/timing checks at both scales. None of those results
substitutes for difficult-area coverage or final source/binary qualification.

A1/A2 are supported by the combined cost, capture and lifetime evidence above,
not this source audit alone. A6 is complete for the retained 1x scope; see the
retention report for final identity and scaled fallback. The CPU producer before packet
emission remains unidentified; the implementation uses the verified GPU draw
boundary and retains guest command processing.
