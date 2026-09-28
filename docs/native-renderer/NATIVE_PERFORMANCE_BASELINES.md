# Native renderer performance baselines

`native` results per route, with the Xenos baselines they were measured
against for the Xenos retirement backlog (XR-00, XR-07, XR-09). The Xenos
rows are historical measurements taken before the Xenos renderer was
removed (`6b75238`); they cannot be rerun on the current build. Compare new
native runs with the native rows, run back to back with a control on the
same build where possible. Runs come from seed `appdata-2026-09-27`
through `tools/run-fh1-render-test.py --configuration RelWithDebInfo --hidden
--seed-pipeline-prewarm` with the shareable FH1 shader pack, on the
development machine (NVIDIA, vendor 10DE). Frame and GPU times come from the
per-frame performance CSV, skipping the first 120 rows. For the retirement
backlog, a regression was a native median or p95 more than 3% above Xenos.

Routes had to reach the same content on both renderers, so they synchronize on
game state (`wait` steps) instead of wall time: `fh1-race-sync` waits for the
car, the car-select thumbnails and the race's replay stream;
`fh1-modes-sync` waits for the car before driving, pausing, opening the map
and entering photo mode. `fh1-fmv` plays the opening movies. The older
wall-clock routes (`fh1-race`, `fh1-free-roam`, `fh1-map`, `fh1-pause`,
`fh1-photo-mode`) drift: the native and Xenos runs of `fh1-race` reached
different events, so they are not used for comparisons.

## Whole routes

| Route, renderer | Frames | Median ms | p95 ms | p99 ms | GPU median ms | GPU p95 ms | Draws/frame |
| --- | --- | --- | --- | --- | --- | --- | --- |
| fh1-fmv xenos | 1163 | 4.01 | 6.00 | 6.52 | 0.70 | 1.64 | 53 |
| fh1-fmv native | 1137 | 4.06 | 6.01 | 6.53 | 0.74 | 1.64 | 54 |
| fh1-fmv xenos (repeat) | 1132 | 4.10 | 6.01 | 6.52 | 0.64 | 1.63 | 53 |
| fh1-fmv native (repeat) | 1104 | 4.15 | 6.01 | 12.11 | 0.62 | 1.60 | 54 |
| fh1-race-sync xenos | 4749 | 13.78 | 28.08 | 33.60 | 5.54 | 11.94 | 2469 |
| fh1-race-sync native | 4751 | 13.58 | 27.49 | 33.52 | 5.89 | 13.04 | 2506 |
| fh1-modes-sync xenos | 4231 | 16.52 | 21.89 | 28.34 | 7.41 | 8.45 | 3781 |
| fh1-modes-sync native | 4217 | 15.59 | 21.55 | 29.14 | 8.12 | 9.71 | 2815 |

## Race window

The last 600 frames of `fh1-race-sync` are the moving race (about 5,800
draws per frame on both):

| Renderer | Median ms | p95 ms | GPU median ms | GPU p95 ms |
| --- | --- | --- | --- | --- |
| xenos | 25.19 | 30.23 | 11.60 | 12.04 |
| native | 24.95 | 30.15 | 12.61 | 13.00 |

Frame time was at parity (-1.0% median, -0.3% p95); race frames are
CPU-bound, and native GPU time was about 1 ms higher, mostly EDRAM ownership
transfers (depth ping-pongs between MSAA modes at one base that the scene
really needs, and color-to-depth transfers). Run-to-run p95 varies by up
to 8% on this route, so compare pairs run back to back.

Earlier builds for reference: before repeated-target preparation was
skipped, native prepared targets for 3.1 ms per race frame and the race
window was +1.8% median; before per-tile stencil state, the whole route was
+3.3-3.7% median.

## Race frame cost breakdown

Measured for [NP-2.0](../NATIVE_PORT_BACKLOG.md#np-2-fast-frame-pass-1)
on `fh1-race-sync` (seed `appdata-2026-09-27`, 1x pack, `RelWithDebInfo`,
`--hidden`), one run per instrument, over the moving race at the end of
the route:

| Cost | Race value | Source |
| --- | --- | --- |
| Deferred command-tape replay | 3.44 ms median, 5.46 ms p95 per frame (upper bound: wall time around `DeferredCommandList::Execute`) | `--perf_critical_path_trace=true`, `tools/summarize-critical-path-trace.py`, source frames 4640-4885 |
| Frame interval in that trace | 23.63 ms median, 29.39 ms p95 | same |
| Texture reloads a resolve invalidated | 87-88 per frame, 64-71 MB of guest data untiled per frame; 87 of the 89 dirty texture loads per frame | `texture_resolve_reloads`, `texture_resolve_reload_bytes`, last 600 frames |
| GPU time of those reloads | 0.39-0.56 ms per frame; all other texture loads 0.01 ms | `--fh1_native_gpu_profile=true` (`texture_reloads`, `texture_loads`) |
| Executor GPU phases | transfers 0.81-0.98, resolves 0.33-0.43, clears 0.10-0.16 ms per frame | same |
| Executor CPU phases | prepare_targets 0.59-0.72 (transfers 0.26-0.31), bind_targets 0.29-0.37, resolves 0.31-0.46 ms per frame | same |

The tape replay is the largest single item: about 15 % of the race frame
runs after consumption on the GPU commands thread, so direct recording or
overlapped replay (NP-2.6) leads the CPU work. Resolve round trips are the
largest GPU item after transfers (NP-9.1).

Stencil use per depth surface over the last 10 census windows
(`--fh1_frame_census=true`, `tools/summarize-fh1-stencil-census.py
--first-frame 4280`):

| Depth surface | Draws | Stencil on | May write nonzero | Windows writing |
| --- | ---: | ---: | ---: | ---: |
| D24FS8 at tile 1024, 4x, pitch 1280 | 2,276,687 | 2,272,055 | 692,803 | 10 / 10 |
| D24FS8 at tile 0, 1x, pitch 1280 | 718,282 | 717,682 | 332,247 | 10 / 10 |
| D24S8 at tile 720, 1x, pitch 1040 | 121,884 | 1,002 | 0 | 0 / 10 |
| D24FS8 at tile 128, 2x, pitch 320 | 107,543 | 107,543 | 0 | 0 / 10 |

Both scene depth surfaces write nonzero stencil in every window (REPLACE
with a per-object reference such as 0x15, 0x18, 0x03 under write mask
0xFF), so skipping the eight stencil-bit transfer passes for an unwritten
source cannot help the dominant depth ping-pong between them; only the
smaller surfaces qualify. A single stencil pass that exports the reference
from the pixel shader, where the device supports it, is the better target
for NP-2.4.

### GPU commands thread attribution

A WPR capture of the same route (`tools/capture-cpu-profile.ps1` from seed
`appdata-2026-09-27` with the 1x pack, zero pack misses; 273,123 samples,
183 without stacks, no lost events) over the last 600 source frames
(25.76 ms per frame):

| Thread | CPU per frame | Busy |
| --- | ---: | ---: |
| GPU commands (`CommandProcessor::WorkerThreadMain`) | 23.09 ms | 90 % |
| Title render thread (`sub_8259F3E8`) | 24.07 ms, of which 9.88 ms polls `sub_829F04A8` | 93 % |
| Title simulation (`sub_823ED888`) | 15.80 ms | 61 % |
| Audio (`sub_82FB4AF8`) | 6.81 ms | 26 % |

The title's poll waits for the word `EVENT_WRITE_SHD` stores on the GPU
commands thread, so the race frame is bound by that thread. Its time per
frame, inclusive:

| Work | ms per frame | Largest parts |
| --- | ---: | --- |
| `D3D12CommandProcessor::IssueDraw` | 11.56 | `UpdateBindings` 2.24 (1.13 self, sampler parameters 0.41), `SharedMemory::RequestRange` 2.22, `RequestTextures` 1.63, `PrimitiveProcessor::Process` 1.41, `Fh1NativeExecutor::PrepareTargets` 0.84 and `BindTargets` 0.52, `perf::IncrementCounter` 0.60, `ConfigurePipeline` 0.44 |
| `IssueSwap`, `EndSubmission`, `DeferredCommandList::Execute` | 3.86 | `d3d12core` 2.29, NVIDIA user-mode driver 1.39 (the recorded `OMSetRenderTargets`, root and draw calls; the driver has no public symbols, so single D3D12 calls are not separable) |
| Type-0 register writes (`WriteRegisterRangeFromRing`) | 3.69 | `WriteRegistersFromMem` 3.08, `RegisterFile::GetRegisterInfo` 0.78 self |
| `SharedMemory::RequestRanges` | 3.65 | `UploadRanges` 2.27 (memcpy 1.69, `MakeRangeValid` 0.51), range-vector reallocation and allocator 0.82 |

`perf::IncrementCounter` costs 0.65 ms per frame on this thread, and all
`Fh1NativeExecutor` work together 1.93 ms.

### After NP-2.7 and NP-2.6

Each change was measured on `fh1-race-sync` (seed `appdata-2026-09-27`,
1x pack, `RelWithDebInfo`, `--hidden`) as interleaved pairs, race window
= last 600 frames:

| Change | Control median | Candidate median | Notes |
| --- | --- | --- | --- |
| NP-2.7 bookkeeping (DLL swap) | 24.30, 25.28 ms | 21.13, 21.21 ms | Undisturbed runs; one run per arm had 30-116 frames over 40 ms |
| NP-2.6 async submission (`--d3d12_async_submission`) | 21.05-21.19 ms | 19.25-20.32 ms | p95 25.52-26.15 vs 22.34-25.18 ms; asynchronous runs render about 5 % more draws per race frame |

The two sets were taken in different sessions, so their product (about
-20 % from 24.8 to 19.9 ms) is indicative, not an A/B.

## Resolution scale

`fh1-race-sync` at 2x and 3x, each renderer with a pack produced at that
scale by the native renderer:

| Scale, renderer | Median ms | p95 ms |
| --- | --- | --- |
| 2x xenos | 14.51 | 29.81 |
| 2x native | 14.11 | 28.53 |
| 3x xenos | 21.25 | 34.17 |
| 3x native | 17.08 | 29.72 |

Native was faster at both scales (-2.8% and -19.6% median); the cause of
the 3x gap was not profiled. Scales other than 1x, 2x and 3x fail graphics
setup on the native renderer.

## Memory

`FH1 native executor memory MB` (logged with the executor stats): textures
stay under the texture cache's soft limit of 384 MB (race 265, photo 178,
free roam 149 MB peaks); executor surfaces take 522-562 MB and the transfer
word buffer 10 MB. On the race route, surfaces take 2024 MB at 2x and
4448 MB at 3x, textures 383 and 551 MB (the scaled resolve range is
counted with textures).

## Not covered

Only this machine was measured. AMD, Intel and lower-end GPUs are not
available to the project, so XR-09's hardware item stays open until someone
with that hardware runs these routes.
