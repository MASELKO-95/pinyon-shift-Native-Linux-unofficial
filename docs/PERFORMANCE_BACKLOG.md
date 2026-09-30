# Performance backlog: 4K at 120 fps on Vulkan

Status: **open; created 2026-09-30** at `dev` checkpoint `58473da` (ShiftGlue
`b9a5de0`). This backlog replaces the performance items of the
[native port backlog](NATIVE_PORT_BACKLOG.md) (NP-2, NP-3, NP-9 and the
speed half of NP-15) with one target and one plan. It was written from a
read-only audit of the renderer, the command processor, the recompiled guest
code and the frame loop, plus the measurements in
[Where the frame stands](#where-the-frame-stands). Paths use `sdk/` for
`thirdparty/shiftglue-sdk/`. Raw run output is kept locally under
`.local/perf4k/` and is not distributed.

## Target

The moving race at a **3x internal scale (3840x2160) at a steady 120 fps**
on the maintainer's machine (Ryzen 7 5800X, RTX 4080, 4K display at
120 Hz), on the **Vulkan** backend, with D3D12 kept working but not tuned.
A frame is 8.33 ms, and FH1 ends its frames on guest vblanks, so a frame
that misses the budget costs a whole vblank (4.17 ms at the 240 Hz vblank
the render limit of 120 gives): the measured race frames take 8.4, 12.5,
16.7, 20.8 or 25.0 ms and nothing in between. The budget below therefore
leaves margin for jitter.

The maintainer has accepted, for this target, results that are not 100 %
faithful to the console and optimizations that carry risk, as long as the
gates in [PB-5](#pb-5-gates-and-protocol) hold. The trades this plan makes
are listed in [Fidelity trades](#fidelity-trades-this-plan-allows).

## Budget

Every stage runs concurrently with the others, so each must fit the frame
on its own.

| Stage | Today (1x unless noted) | Budget per frame | Margin note |
| --- | ---: | ---: | --- |
| GPU, one race frame at 3x | 29 ms on Vulkan, 18.5 ms on D3D12 | **≤ 7.0 ms** | includes the presenter's passes |
| GPU commands thread (PM4 decode, state derivation, executor, tape) | about 18 ms busy on Vulkan (the 1x frame; 5,800-6,400 draws) | **≤ 6.0 ms** | measured with `pinyon_shift_thread_sampler` |
| Submission worker (tape replay, `vkQueueSubmit`) | ~3 ms | ≤ 5.0 ms | grows with descriptor work moved onto it |
| Title render thread (`Guest 825A6320`, the loop in `sub_8259F3E8`) | 6.5-7.8 ms (39 % busy at 60 fps; 27 % of a 29 ms frame at 3x) | **≤ 6.0 ms** | per frame; recompiled code |
| Title simulation thread (`Guest 8255AE10`, `sub_823ED888`) | 4.6-4.7 ms per step (55 % busy at 120 steps/s) | ≤ 6.0 ms per step | one step per frame at 120 fps; steps follow vblanks, not frames |
| Audio (`Guest 82FB4AF8`) and the rest | 29 % of a core | no change | |
| Present | one 4K blit | ≤ 0.3 ms GPU | no scaling at 3x on a 4K display |

## Where the frame stands

Measured 2026-09-30 on `fh1-race-sync` (seed `appdata-2026-09-27`,
`RelWithDebInfo`, hidden window, `--pinyon_shift_fh1_render_fps_limit=120`
so the guest vblank runs at 240 Hz, car-card repair off), race window =
last 600 frames less the final 30, D3D12 packs `fh1-native-v3` at 1x and
3x. GPU utilization is `nvidia-smi` sampled every 500 ms over the race.

| Run | Frame median / p95 ms | fps | GPU span median ms | GPU busy | Draws/frame | Sim steps/frame |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| D3D12 1x | 18.10 / 26.0 | 55 | 16.5 | 35 % (p10 23 %) | 6,357 | 2.3 |
| D3D12 1x, render limit 60 (vblank 120 Hz) | 16.70 / 25.4 | 60, capped | 15.0 | 33 % | 5,925 | 1.1 |
| D3D12 3x | 19.22 / 25.0 | 52 | 18.5 | **89 % (p10 75 %)** | 6,135 | 2.3 |
| D3D12 3x, `--fh1_native_gpu_profile=true` | 24.78 / 30.8 | 40 | 24.1 | 71 % | 5,904 | 3.0 |
| D3D12 1x, `--fh1_native_gpu_profile=true` | 20.00 / 25.5 | 50 | 17.6 | 37 % | 6,195 | 2.4 |
| Vulkan 1x | 20.79 / 25.9 | 48 | not written on Vulkan (PB-0.1) | 36 % (p10 30 %) | not counted on Vulkan | 2.5 |
| Vulkan 3x | **29.13 / 33.6** | 34 | not written | **98 % (p10 99 %)** | not counted | 3.5 |
| Vulkan 1x, `--fh1_native_gpu_profile=true` | 20.83 / 28.2 | 48 | executor span 18.8 | | | 2.6 |
| Vulkan 3x, `--fh1_native_gpu_profile=true` | 30.70 / 36.9 | 33 | executor span 30.3 | | | 3.7 |

What the numbers say:

- **At 1x the race is CPU-bound at about 18 ms on D3D12 and 21 ms on
  Vulkan** with the GPU a third busy; at 3x the GPU becomes the limit at
  18.5 ms on D3D12 and **29 ms on Vulkan** (99 % busy) with the CPU side
  unchanged. The CPU side needs 2.2-2.5x, the Vulkan GPU at 3x about 4x.
- **Vulkan's GPU frame is 57 % longer than D3D12's for the same draws** at
  3x (29.1 against 18.5 ms) and its transfers alone are 10.3 against
  7.7 ms profiled, so part of the Vulkan gap is backend overhead rather than
  emulation cost (PB-1.9).
- **The old "60 fps cap" was the vblank.** With the render limit at 60 the
  guest vblank runs at 120 Hz and FH1, which waits two vblanks per frame,
  lands on 16.70 ms every frame; with it at 120 the cap moves to 8.33 ms
  and the race runs unpaced at 55 fps. The hidden window reports 60 Hz, so
  every earlier hidden baseline was capped.
- **Frames are quantized to vblanks.** In the unpaced 1x run, 14 frames
  took 2 vblanks (8.37 ms), 291 took 4 (16.70), 203 took 5 (20.79) and 71
  took 6 (25.00); a frame that misses 8.33 ms by a little costs 4.17 ms.
- **Simulation steps follow vblanks, not frames.** FH1 steps its
  simulation once per two vblanks: 120 steps/s here regardless of the
  frame rate, 2.3 steps per 18 ms frame. Frames with 3 steps take 21.5 ms
  median against 17.1 ms with 2, so the simulation thread is on the
  critical path when a frame spans more vblanks, which is the "catch-up"
  behaviour reported at 4x (NP-4.10).
- **The GPU profile flag costs 2-5 ms per frame** (timestamp queries and
  per-draw timers: 18.1 vs 20.0 ms at 1x, 19.2 vs 24.8 ms at 3x), so its
  phase times are upper bounds; the frame numbers above come from runs
  without it.
- **Thread map.** `Guest 8255AE10`'s start routine calls `sub_823ED888`,
  the simulation loop; `Guest 825A6320` runs `sub_825A6208`, which calls
  the render loop `sub_8259F3E8`; `Guest 82FB4AF8` is audio.
- **Thread sample at 3x on Vulkan** (`pinyon_shift_thread_sampler`,
  4 ms interval, race window of a 29.0 ms frame, `.local/perf4k/vk3x-sampled`):
  GPU commands thread 70 % busy (about 20 ms per frame; its 30 % of waits
  are GPU fences and the worker, since the GPU is the limit), submission
  worker 23 % (mostly `vkQueueSubmit` blocking behind the saturated GPU),
  simulation thread 55 % (3.4 steps per frame, 4.7 ms per step), render
  thread 27 % (7.8 ms per frame; 68 % of its time in the fence wait
  `sub_829F04A8` -> `sub_823E91F0`), audio 28 %, and about 0.5 core across
  fourteen other guest threads. On the GPU commands thread the largest
  exclusive items are the NVIDIA driver (6.3 %, about 1.8 ms, no public
  symbols: descriptor writes), `UpdateBindings` self 4.8 %,
  `IssueDrawImpl` self 3.5 %, the type-0 register path (`WriteRegister`
  4.7 %, `WriteRegistersFromMem` 2.3 %, `copy_and_swap_32_unaligned`
  1.6 %) and `LoadShader` 1.5 %.

Executor GPU phases with the profile on, ms per frame over the race
window (upper bounds, see above):

| Phase | D3D12 1x | D3D12 3x | Vulkan 1x | Vulkan 3x | Scaling |
| --- | ---: | ---: | ---: | ---: | --- |
| Ownership transfers | 1.39 | 7.70 | 1.43 | 10.27 | pixels x samples x passes (9 for depth with stencil) |
| Resolves | 0.63 | 2.08 | 0.66 | 2.30 | pixels x samples |
| Texture reloads a resolve invalidated (untile) | 0.73 | 2.61 | not timed on Vulkan | not timed | pixels |
| Clears | 0.21 | 0.26 | 0.20 | 0.28 | pixels x samples |
| Sum | 2.96 | 12.65 | 2.29 + reloads | 12.85 + reloads | of a 17.6 / 24.1 / 18.8 / 30.3 ms profiled span |

At 3x the executor's own passes are about half the profiled GPU frame and
the draws the other half on both backends. Memory at 3x: surfaces
4,448 MB, scaled resolve range 464 MB, textures 558 MB, transfer words
90 MB.

Transfer census at 3x over the 4,800-frame run (`transfer tile-passes`
log line; `base/pitch/msaa/kind`): depth 4x to 1x at base 0 11.66 M tile
passes, depth 2x to 4x at base 128 10.37 M, depth 1x to 4x at base 0
7.78 M, depth 128/4x to 1024/4x 5.53 M, colour (720) to depth (128, 2x)
3.89 M, colour (720) to depth (0, 4x) 3.89 M. Totals: 871,462 transfers
(181 per frame) in 96,511 batches, 253 M tile passes (53,000 per frame),
stencil passes skipped on 68 % of transfers.

## Gap analysis

| Stage | Today | Budget | Factor | Where the time is |
| --- | ---: | ---: | ---: | --- |
| GPU at 3x | 29 ms Vulkan (18.5 ms D3D12) | 7.0 ms | 4.2x (2.6x) | half in the executor's emulation passes (transfers, resolve, untile, clears), half in the draws on 4x-MSAA 4K surfaces; Vulkan adds backend overhead on both halves |
| GPU commands thread (Vulkan) | ~17 ms | 6.0 ms | 2.8x | per-draw descriptor sets and `vkUpdateDescriptorSets`, register writes, shared-memory range checks, texture and sampler re-derivation, pipeline lookup, swap wait |
| Title render thread | 6.5-7.8 ms | 6.0 ms | 1.1-1.3x | recompiled code: register file in memory, volatile guest accesses, MXCSR toggles; waits on the GPU thread's fence words |
| Simulation step | ~4.6 ms | 6.0 ms | fits | must stay one step per frame (PB-4) |
| Frame pipeline | 4-6 vblanks per frame | 2 | — | the title runs one frame ahead of the GPU commands thread and both end on vblank ticks; the GPU thread's sleep quanta cost 1-2 ms; the simulation's millisecond delta jitters and has no step cap |

## Fidelity trades this plan allows

Accepted by the maintainer on 2026-09-30 for the 4K target; each is a
setting with the faithful behaviour available, and each is checked by eye
on captures rather than by byte-identical replays.

1. **Single-sampled host surfaces at scale 2x and above.** The guest's 4x
   and 2x MSAA surfaces become 1-sample (or 2-sample) images. At 3x each
   guest pixel is still 9 host pixels, but 4K edges lose MSAA and
   alpha-to-coverage foliage and fences go hard-edged; `swap_post_effect
   = fxaa` is the cheap cover.
2. **Stencil dropped from ownership transfers** where no draw tests stencil
   on the destination before its next clear, and transfers skipped where a
   golden replay at 1x proves the destination is fully overwritten before
   it is read.
3. **Resolves that bypass the guest memory mirror** except for the resolves
   the CPU reads (thumbnails, one-off captures).
4. **Guest numerics changes** (codegen register locality, vector lowerings,
   an AVX2 baseline) gated by the pose-drift and save-payload-hash checks,
   not by bit-identical traces.
5. **Frame-loop hooks** that cap the simulation steps a frame may run and
   pin per-frame animations to real time.

## PB-0 Measure and instrument

Everything below is a prerequisite for judging the items after it; each is
small.

| Item | Work | Size |
| --- | --- | --- |
| PB-0.1 | Vulkan per-frame GPU timing: the Vulkan command processor fills neither `guest_frame_gpu_time_ns` nor `draw_calls` in the performance CSV (both zero in every Vulkan run) and charges no fence or worker wait to `gpu_thread_fence_wait_ns` (`sdk/src/graphics/d3d12/command_processor.cpp:2932, 3488` only). Add frame-open and swap timestamps, the draw counter and the wait counters on Vulkan. | S |
| PB-0.2 | Vulkan executor phase parity: `texture_reloads`/`texture_loads` GPU phases (D3D12 `fh1_native_executor.cpp:2113-2119`) and the transfer top-pairs log (`transfer_volume_`, D3D12 `:776-780, 2121-2129`) exist only on D3D12; add both to the Vulkan executor, plus per-frame counts of renderings begun and barriers submitted. | S |
| PB-0.3 | Thread attribution at the target rate. **Sampled 2026-09-30** (the busy shares above): `pinyon_shift_thread_sampler` over `GPU Commands`, `GPU Submission` and `Guest ` at 3x on Vulkan with the render limit at 120 (`.local/perf4k/vk3x-sampled`). Still to do: the same sample with `--lines 1` mapped to guest instructions for the render and simulation threads (PB-3.1), and count the CPU-visible PM4 packets per frame (`EVENT_WRITE_SHD`, `MEM_WRITE`, `COND_WRITE`, `REG_TO_MEM`, `XE_SWAP`; the packet recorder at `sdk/src/graphics/command_processor.cpp:722-747`), which bound the decode-to-record pipeline depth of PB-2.11. | S |
| PB-0.4 | One Nsight Graphics GPU trace of a 3x race frame on Vulkan to split the draws' GPU time between rasterizer/ROP, shading and front-end, and to time the transfer passes and the words compute individually. Decides how much PB-1.1 (fewer samples) against PB-1.8 (faster shaders) is worth. | S (tool install) |
| PB-0.5 | Captures at 3x for fidelity checks: frame dumps and replays are gated to 1x (`command_processor.cpp:1078-1083`), so the aggressive items are judged by 3x route captures (`fh1-race-sync`, `fh1-free-roam`) compared by MAE against the faithful setting, with the 1x golden replays kept for bit-exact changes. Add a `fh1_debug_skip_transfers=<n>-<m>` cvar next to `fh1_debug_skip_draws` (`command_processor.cpp:54-63`) so transfer classes can be skipped in a replay and diffed. | S |
| PB-0.6 | The protocol for every number in this backlog: hidden, seed `appdata-2026-09-27`, `fh1-race-sync` last 600 frames less 30, render limit 120, GPU profile off, three interleaved pairs for an A/B, `nvidia-smi` sampled alongside, reported as frame median and p95, GPU span, GPU busy, draws, vblanks and simulation steps per frame (`.local/perf4k/measure4k.ps1` is the reference script). | S |

## PB-1 GPU at 3x

Goal: a 3x race frame at or under 7 ms of GPU time on Vulkan. The
executor's passes are the first half, the draws on 4x-MSAA 4K surfaces the
second.

| Item | Work | Expected | Size |
| --- | --- | --- | --- |
| PB-1.1 | **Single-sampled host surfaces at scale.** `GetOrCreateSurface` makes a `width*scale x height*scale` image with `samples = 1 << key.msaa` (`sdk/src/graphics/vulkan/fh1_native_executor.cpp:546, 571-575`) and `BindTargets` puts `msaa_samples` in the pipeline key (`:1449`; pipeline `sdk/src/graphics/vulkan/pipeline_cache.cpp:3419-3437`), so at 3x the scene's 4x surfaces are 3840x1536 images with 4 samples: 36 samples per guest pixel for ROP, depth, clears, transfers and resolves. Add a host sample mode for scale >= 2 (`fh1_host_msaa_mode`: native, 2, 1): image samples 1 or 2, `msaa_samples = k1X` in the render-pass key, `LayoutConstant`/`HostSample`/`GuestSample` remapped (`fh1_native_edram.hlsli:225-245, 262-330`), the non-multisampled transfer and resolve variants selected (`:640-647, 1680-1689`), `EnsureTransferWords` at 1 sample. Guest shaders need no retranslation: `kSysFlag_MsaaSamples` matters only to the FSI and sample-rate paths (`spirv_translator.cpp:3094, 3184-3195`). The 2-sample mode is the inverse of today's 2x-as-4x mapping. | ROP, depth and every executor pass on the 4x surfaces (12.5 M of 19 M draws) at a quarter of the sample traffic; the largest single GPU lever | M |
| PB-1.2 | **Resolve aliasing on Vulkan** (NP-9.1, deferred at 1x for 0.5 ms; worth 3-5 ms at 3x). Resolve into a storage-image view of the destination `VulkanTexture` (A2B10G10R10, R8G8B8A8, R32F, RGBA16F are storage-capable on NVIDIA) and mark its payload generation loaded so `FindOrCreateTexture`/`LoadTextureData` skips the untile (`:1832-1960`, `MarkRangeAsResolved` at `:1947`; `pipeline/texture/cache.cpp:347-375, 476-515`). Keep the mirror write for one-off resolves the CPU reads, `vulkan_readback_resolve` and frame dumps, and fall back to the mirror when a later fetch reads the range through a different key. First step, cheap on its own: port D3D12's direct reflection-cube import (`d3d12/texture_cache.cpp:2250-2300`). | removes the 88 resolve-sourced reloads per frame (2.6 ms profiled at 3x) and most of the resolve pass; removes the front buffer's 33 MB round trip | L |
| PB-1.3 | **Transfers without nine passes.** NVIDIA exposes neither `PSSpecifiedStencilRef` on D3D12 nor `VK_EXT_shader_stencil_export` on Vulkan (checked with `vulkaninfo`, driver 581.8), so the one-pass stencil export of NP-2.4 is AMD- and Intel-only. On single-sampled destinations (PB-1.1) the stencil byte can instead be copied from the transfer words buffer into the image's stencil aspect with `vkCmdCopyBufferToImage` (D3D12: `CopyTextureRegion` to plane 1), so a depth transfer becomes one depth draw and one copy instead of the loop at `:1155-1194`; multisampled destinations keep the bit passes. Then: skip the stencil passes for destinations no draw stencil-tests before their next clear (per-surface stencil-read census in the executor), and skip whole transfers a 1x golden replay proves unread (PB-0.5), starting with the two biggest pairs, the 4x/1x depth reinterpretation at base 0 and the 2x/4x one at base 128. Fewer transfers also remove their words dispatches, barriers and rendering restarts. | transfers from 7.7 ms profiled at 3x to about 1 ms | M |
| PB-1.4 | **Present chain.** Today: HUD surface -> resolve to the mirror (33 MB at 3x) -> untile reload of the 2_10_10_10 swap texture -> `apply_gamma_pwl` into the presenter's guest-output image -> bilinear quad into the swapchain (`vulkan/command_processor.cpp:2497-2622, 2827-2846`; `vulkan_presenter.cpp:1723-1990`). Sample the executor surface that owns the front buffer's tiles (or the PB-1.2 texture) in the gamma pass, and write the swapchain directly when no scaling or CAS/FSR is selected. | four full-frame 4K passes removed, about 0.3-0.6 ms | S-M |
| PB-1.5 | **Renderings and barriers.** One dynamic-rendering scope per change of bound surfaces or per pending barrier, LOAD/STORE always; texture loads call `SubmitBarriers(true)` per dispatch (`vulkan/texture_cache.cpp:1652`), shared-memory uploads end the rendering (`vulkan/shared_memory.cpp:283`), executor transitions are full-image barriers (`:469-533`). NP-12.4 counted about 300 renderings and 600 barriers per race frame; each drains a 4K pipeline. Batch the texture-load barriers per load, upload shared memory before the rendering starts, count both per frame (PB-0.2). | 0.5-2 ms | S-M |
| PB-1.6 | **Clears** through `loadOp = CLEAR` where a resolve-clear or a full-surface clear precedes the next rendering, instead of `vkCmdClearAttachments` inside its own rendering (`:1558-1631`). | small; free once PB-1.1 lands | S |
| PB-1.7 | **Draw-side GPU cost.** After PB-0.4: if ROP-bound, PB-1.1 covers it; if shading-bound, PB-1.8; if front-end-bound (about 6,000 draws with pipeline, descriptor and dynamic-state changes each), nothing short of merging draws helps and the floor is measured. | decides the order of the rest | — |
| PB-1.8 | **Translated shader efficiency**, from an audit of the SPIR-V translator and the disc corpus (12,846 programs: 10,818 vertex, 2,028 pixel). The pixel shaders average 4.1 texture fetches and 47.6 ALU instructions, half of them `mul`/`mad`; everything below scales with pixels and applies to the DXBC path too where noted. (a) **Texture fetches** (`spirv_translator_fetch.cpp`): every computed-LOD fetch samples with explicit gradients (`OpDPdx/DPdy` scaled by `exp2(lod)`, `:1467-1636`) only to fold the fetch constant's LOD bias in, a rounding epsilon is always applied (size decode from the fetch constant, an `FDiv` and `FAdd` per axis, `:694-707, 847-941, 1088-1094`), signedness is resolved at run time with a second sample under `if (is_any_signed)` and a per-component `OpSwitch` (`:1382-1409, 1966-2043, 2207-2246`), then an exponent-bias `Ldexp` and `FMul` per component. Extend the cube change (SDK `9e34a49`) to 1D/2D/3D: implicit LOD with `Bias`; apply the epsilon only to point-filtered fetches; specialize signedness and bias per (shader, fetch constant) with a runtime fast path first and a mismatch counter before removing the fallback. (b) **ALU** (`spirv_translator_alu.cpp:190-278`, `spirv_builder.cpp:42-53`): every float op carries `NoContraction`, so the driver never forms an FMA, and every `mul`/`mad` adds the SM3 zero check (`NMin(|a|,|b|) == 0 ? 0 : a*b`), about five ops instead of one FFMA on half the pixel ALU. Drop both for pixel-shader colour math behind a cvar (keep them for vertex position math and anything feeding depth); the same change applies to DXBC (`dxbc_translator_alu.cpp:85-100`, `dxbc_translator.cpp:3125-3131`). Risk: shaders relying on `0 x Inf = 0` masking (the HDR paint and glass shaders were exactly this sensitive); validate with the goldens at tolerance, the free-roam frame 1800 glow count and register captures on `BDA312E0D00025E9` / `410E568A69EC236A`. (c) **Control flow** (`spirv_translator.cpp:593-705, 1299-1324, 1425-1616`): programs with jump targets become one `OpLoopMerge DontUnroll` loop with a per-lane program counter and an `OpSwitch` (`DontFlatten`) over the labels, so every temporary is loop-carried and the driver cannot propagate or eliminate across cases. 372 of 2,028 pixel shaders have jumps (511 forward, 2 backward, 10 loops), all reducible to nested `if`/`if-else`; vertex shaders are 99.2 % label-free. Emit forward jumps as `OpSelectionMerge` regions (bool-constant jumps uniform, predicated ones keeping the quad-uniform AND from SDK `dfed4c8`), loops as real loops, and keep the PC loop as the fallback for the two backward-jump shaders; drop `DontFlatten` on short exec conditionals. (d) **Resolution scale as specialization constants** (`fetch.cpp:993-1004`, `spirv_translator.cpp:3150-3171, 3262-3301`; `pSpecializationInfo` is null at `pipeline_cache.cpp:3213-3267`): identical code after folding, one pack for every scale, which is NP-4.9 for shaders. (e) **Vertex fetch**: endianness is two runtime branches per dword (`:3997-4064`) and `robustBufferAccess` is on (`vulkan_device.cpp:647`), which NVIDIA implements with per-access bounds checks; make the endianness a specialization constant, bind shared memory as an `R32_UINT` texel buffer (NVIDIA's 2^27-texel limit is exactly 512 MB; verify) and turn robust access off. (f) Never enable `depth_float24_convert_in_pixel_shader` (`flags.cpp:20`): it forces sample-rate shading, four pixel-shader invocations per pixel at 4x. | (a) and (b) high at 3x: a large share of pixel time; (c) medium-high on the expensive material shaders; (d) packaging; (e) low-medium | (a) S-M, (b) S, (c) M, (d) S, (e) S |
| PB-1.9 | **Vulkan is slower than D3D12 on the same GPU work** (3x profiled: executor frame 30.3 ms against D3D12's 24.1 ms, transfers 10.3 against 7.7 ms). Candidates, in order: the rendering restarts and full-image barriers around transfers and texture loads (PB-1.5), `robustBufferAccess` (PB-1.8e), and the per-draw dynamic state; PB-0.4's trace settles it. | closes the backend gap before the levers above | S to find |

## PB-2 GPU commands thread on Vulkan

Goal: under 6 ms busy for a 6,000-draw frame. Today about 17 ms: draws
59 % (2.0 us per draw), of which `UpdateBindings` 16 % with the driver's
descriptor allocation and writes 40 % of that; register writes, dispatch,
texture and sampler re-derivation, shared-memory range checks and the swap
wait make up the rest. Three tiers; the first is low-risk single-thread
work, the second moves decode off the recording thread, the third is the
native draw ABI.

**Tier 1, single-thread (estimated 17 to about 10 ms):**

| Item | Work | Removes | Size |
| --- | --- | --- | --- |
| PB-2.1 | Descriptors: every draw rebuilds both texture sets (`vulkan/command_processor.cpp:7038-7041`), allocates a new constants set with five writes whenever any constant buffer changed (`:7115-7149`) and issues one `vkUpdateDescriptorSets` (`:7196`). Use `VK_KHR_push_descriptor` (exposed by the driver) for the texture and sampler sets, recorded into the tape so the driver work lands on the submission worker, and make the constants set `UNIFORM_BUFFER_DYNAMIC` with one set per 2 MiB upload page and five dynamic offsets per draw (`CmdVkBindDescriptorSets` already carries offsets, `deferred_command_buffer.h:63-90`). No SPIR-V change. `VK_EXT_descriptor_buffer` (also exposed) is the follow-up if the worker becomes the limit. | about 1.5 ms on this thread | M |
| PB-2.2 | Register writes: bulk paths exist only for float, bool and fetch constants (`:2260-2308`); every other type-0 range goes through the virtual `WriteRegister` per register with `load_and_swap` (`sdk/src/graphics/command_processor.cpp:380-537`). SIMD `copy_and_swap` for every range, then side effects only for the few special registers (scratch, `COHER_STATUS_HOST`, `DC_LUT_*`, `VGT_EVENT_INITIATOR`) that intersect it. | 1.8-2.2 ms | S |
| PB-2.3 | Samplers: `GetSamplerParameters` and the `samplers_` lookup run for every sampler of every draw (`:3970-4013`, `vulkan/texture_cache.cpp:674-789`); D3D12 gates on the six fetch dwords plus the binding word (`d3d12/command_processor.cpp:4047-4060`). Port the gate. | 0.5-1.0 ms | S |
| PB-2.4 | Shared memory: `RequestRanges` per vertex and index buffer per draw takes the recursive global lock and scans page bitmaps (`shared_memory.cpp:423-568`), re-triggered because fetch-constant writes clear `vertex_buffers_in_sync_` (`:2223, 2306, 4221-4268`). A front cache of ranges validated this frame with an invalidation epoch bumped by `MemoryInvalidationCallback` and `RangeWrittenByGpu`, and vertex-buffer states kept when a fetch constant is rewritten with the same value. | about 0.8 ms | S-M |
| PB-2.5 | Texture bindings: every rewritten fetch constant re-derives its binding (`pipeline/texture/cache.cpp:559-655`) and then `MarkAsUsed`/`SetUsage` and a linear pending-barrier scan (`:3089-3129`). Keep the raw six dwords per binding and compare before deriving; touch usage only when the binding changed within the submission. | 0.5-0.7 ms | S |
| PB-2.6 | State derivation: `UpdateSystemConstantValues` (`:6338-6836`), `GetHostViewportInfo` and `GetCurrentStateDescription` (`pipeline_cache.cpp:1502-1709`, which repeats the polygonal and rasterization tests of `IssueDrawImpl`) recompute every field each draw. A dirty bitset over the ~40 registers they read, set in `WriteRegister`, skips each when clean; the pipeline lookup's XXH3 (`:1102-1165`) follows the same mask. | about 0.7 ms | S |
| PB-2.7 | `LoadShader` hashes the ucode with XXH3 and looks it up on every `IM_LOAD` (`pipeline_cache.cpp:899-921`). Memoize by guest address and dword count, invalidated by a memory watch on the ucode pages. | about 0.5 ms | S |
| PB-2.8 | Swap: the thread waits for the submission worker at every swap (`:5683-5687`) and then refreshes the guest output itself (`:2481-3019`). Queue the presenter refresh as a worker job and split submissions every 256 draws so the last job is short. | 0.5-1.0 ms | M |
| PB-2.9 | Executor per draw: `EstimateMaxY` runs before the signature test, a `std::vector` of bases is allocated per miss, `FindSurface` walks a `std::map` twice and `DrawRenderingId` is computed twice (`fh1_native_executor.cpp:1374-1408, 1447-1456, 1499-1521`). | about 0.2 ms | S |
| PB-2.10 | Fences: `vkResetFences` and fence vectors per submission (`:5636-5680`) against one timeline semaphore. | small | S |
| PB-2.13 | Pipelines: no `VkPipelineCache` exists (`vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, ...)`, `pipeline_cache.cpp:3623`), so every start recompiles the 397 stored pipelines, and a first-seen pipeline creates its placeholder synchronously on this thread (`:1176-1194`). Persist a `VkPipelineCache` next to the `.xpso` storage, and use extended dynamic state (exposed by the driver) for cull, topology, depth-stencil and blend so the 397 descriptions collapse towards the 306 shader pairs. Stutter and start time, not steady frame time. | start time; no hitch on a new pipeline | S-M |

**Tier 2, decode-to-record split (PB-2.11, L).** A decoder thread keeps
PM4 dispatch, register writes into a shadow `RegisterFile`, `WAIT_REG_MEM`
and `LoadShader` hashing, and emits (register run, draw, copy, swap,
CPU-visible packet) records over an SPSC queue; the recorder owns the real
register file, the caches, the tape and submissions. Ordering the code
requires: `EVENT_WRITE_SHD` stores its fence word at decode time
(`command_processor.cpp:1307-1328`) and wakes the title
(`PinyonShiftGpuFenceWait`), after which the title reuses dynamic vertex
and index memory, so either the recorder executes every CPU-visible packet
in order (`EVENT_WRITE_SHD`, `MEM_WRITE`, `COND_WRITE`, `REG_TO_MEM`,
`INTERRUPT`, `XE_SWAP`) or the decoder stalls at each until the recorder
has consumed everything before it; PB-0.3's packet count bounds the depth.
`FlushCpuVisibleResults` must drain the GPU when one-off readbacks are
pending, so the recorder publishes that state. Register side effects
(scratch writeback, `DC_LUT`, `COHER` dirty bit, the fetch/float/bool
invalidations at `vulkan/command_processor.cpp:2190-2308`) replay on the
recorder. Expected: decoder about 2.4 ms plus the `WAIT_REG_MEM` sleeps
that finally overlap recording; recorder about 7 ms after Tier 1.

**Tier 3, native draw ABI (PB-2.12, XL; NP-9.5 revived).** What Tier 1
cannot remove is the per-draw float-constant gather (about 1 ms), the
image-info building for texture sets and the per-draw system constants.
A pack format v4 whose shaders read the raw 256-entry float register array
(one memcpy or delta upload per stage), take texture and sampler indices
from a small per-draw record (bindless; the SPIR-V translator has no
non-uniform-indexed array mode today) and split system constants into
pass-level and per-draw blocks brings the recorder to about 5 ms. This is
the boundary between "Xenia backend with native surfaces" and a native
renderer, and it is what makes 120 fps safe rather than marginal.

## PB-3 Guest CPU

Goal: the render thread's frame and the simulation step each 30 % faster,
so both hold 120 Hz with margin. The audit of the generated code
(76,502 functions, 599,974 load and 431,380 store sites, built with
`-O3 -msse4.1 -mfma -ffp-contract=off -fasync-exceptions`, no LTO or PGO)
finds the register file in memory: every guest register write is a store
to `ctx`, every guest access a `volatile` load or store preceded by a
four-instruction select for the physical-heap skew
(`REX_PHYS_HOST_OFFSET`, `pch_h.inja:140-164`: `stw r12,-8(r1)` is eight
host instructions), every compare writes four condition bytes (227,708
sites, six instructions each), `__savegprlr`/`__restgprlr` are real calls
storing 18 byte-swapped words, 56,860 functions open with a
`switch (ctx.dispatch_address)` of interior-resume aliases (342,683 in
all), MXCSR is toggled with 44,598 conditional checks and 6,090
`ldmxcsr` sites, VMX vectors are byte-reversed with `pshufb` at 42,124
sites and stored back to `ctx` after every op, `vmaddfp` is a separate
multiply and add even on the FMA baseline, and `vmsum3/4fp128` are
35-instruction scalar-double helpers. In a leaf `bdnz` loop
(`sub_82AF85F8`) one store per guest instruction, not the ALU work, limits
throughput. Calls target weak aliases (`sub_X -> __imp__sub_X`) so Clang
cannot inline guest calls even under LTO.

**Why the locality options are off, and how to turn them on.** The
runtime resumes guest code at an interior PC: `XThread::Execute` runs a
`setjmp` loop, and the title's stack switch (`KeSetCurrentStackPointers`
-> `Reenter`) `longjmp`s back to it and re-dispatches at `ctx->lr` with
`dispatch_address` set, so the owning function enters through its resume
`switch` expecting every register in `ctx` (`sdk/src/system/xthread.cpp:776-839`,
`xboxkrnl_threading.cpp:294-299`, `function_dispatcher.cpp:85-87`); guest
`setjmp`/`longjmp` copy the whole `ctx`; two mid-asm hooks take `ctx`
itself. The protocol that makes locals safe: reload localized registers
from `ctx` on the resume path only; spill before `ppc_setjmp`/`ppc_longjmp`
and reload after; spill and reload around the imports that switch context
and the two `ctx` hooks; mark the functions that restore r14-r31 before the
stack switch as `share_registers` (the mechanism exists; find them from the
`stack.reentry.first lr=` trace); keep `skip_lr` off because the runtime
reads `ctx->lr` to re-enter.

| Item | Work | Expected | Size |
| --- | --- | --- | --- |
| PB-3.1 | **Instruction-level profile first.** `pinyon_shift_thread_sampler --lines 1` on `Guest 825A6320` and `Guest 8255AE10` (no elevation needed) mapped with `tools/map-generated-lines.py`, or the WPR capture (`tools/capture-cpu-profile.ps1`, elevated) through `tools/summarize-cpu-hotspots.py`: which guest functions and instruction classes the render thread's 6.5 ms and the simulation step's 4.6 ms are made of. Sizes PB-3.5 and PB-3.7. | decides the order below | S |
| PB-3.2 | **Register locality**: `non_volatile_as_local`, `non_argument_as_local`, `cr_as_local` (then `ctr`/`xer`) with the resume protocol above (`sdk/src/codegen/function_graph.cpp:539-552`, `builders/context.cpp:47-112, 201-232`). Removes the write-through store on most register writes and all 50,077 save/restore calls, and lets Clang forward and eliminate across blocks. Bit-identical. | 15-30 % of guest CPU; the only lever large enough alone | L |
| PB-3.3 | **Offset-free accesses where the address is provably virtual**: stack (r1), TLS (r13), `lis`-derived constants below 0xE000 and image addresses take macro variants without the physical-heap select, tracked per GPR like the existing `mmio_base_regs` (`builder_context.h:41-54`, `context.cpp:544-622`). Guest stacks live at 0x70000000, so the select is dead there. | 4-10 %; mechanical and safe | M |
| PB-3.4 | **Fused `vmaddfp`/`vnmsubfp`/`vmaddcfp128`** on the FMA baseline (`simde_mm_fmadd_ps`, `builders/vector.cpp:93-113`, 8,128 sites): one rounding as VMX defines it, so likely closer to the console than today, but a numerics change gated by pose drift and the save hash; the SSE4.1 baseline keeps the two-op form. | small overall, halves the FP ops of vector loops | S |
| PB-3.5 | **MXCSR toggling**: propagate the flush-mode state across labels and calls (`function_graph.cpp:571`, `context.cpp:379-388`) instead of forgetting it, and measure FTZ/DAZ permanently on as the aggressive variant (changes only double-denormal results). | unmeasured; each `ldmxcsr` serializes | S-M |
| PB-3.6 | **Call overhead**: codegen inlining of tiny leaf callees (no calls, hooks or resume aliases; a TOML denylist keeps hookable functions out) and a cheap out-of-line `if (dispatch_address) goto resume` instead of the full `switch` on entry. | 3-6 % | M |
| PB-3.7 | **Bit-identical SIMD `vmsum3/4fp128`**: `cvtps2pd`, `mulpd`, ordered adds and branchless fix-ups replace the 35-instruction helper (`sdk/include/rex/ppc/intrinsics.h:124-166`), preserving the reduction order; the `ppc_tests` fixtures cover it. | 1-3 %, more in collision code | S |
| PB-3.8 | **Non-`volatile` guest accesses** with compiler barriers at labels, calls and the (currently empty) `sync`/`lwsync`/`eieio`/`isync` lowerings (`builders/system.cpp:40-50`), so spill and reload pairs fold. Risky: polled words and cross-thread flags depend on `volatile` today. | 2-6 % | M |
| PB-3.9 | **Build flags**: drop `-fasync-exceptions` (no SEH scopes are generated), re-measure PGO (`PINYON_SHIFT_RECOMP_PGO`) against guest-thread CPU rather than frame time, AVX2 only for the variable shifts. | small each; free | S |
| PB-3.10 | **Vector lowerings**: `stvlx`/`stvrx`/`stvebx` byte loops and the per-lane `vpkd3d128`/`vupkd3d128`/`vsrw` become mask and blend sequences (`builders/memory.cpp:599-644`, `vector.cpp:997-1010, 1171, 1388`). | minor | S |
| PB-3.11 | **Runtime overheads on guest threads**: count write-watch faults per guest thread (60-70 `VirtualProtect` calls per race frame, each a VEH fault, the global lock and a TLB shootdown), the global-lock contentions (about 260 per frame, every event and wait) and `RtlEnterCriticalSection` spins on the render and simulation threads, then take the direct object pointer in the dispatch header and 64 KiB watch granularity if they show on the critical path. | unknown until counted | S to count, M to fix |

## PB-4 Frame pipeline, pacing and HFR

**How the frame is paced today** (from the generated code and the SDK).
The guest vblank runs on the host `GPU VSync` thread at twice the render
limit or the display refresh (`sdk/src/graphics/graphics_system.cpp:54-58,
189-201`; the limit is hot); each vblank runs the guest's interrupt
callback under the global lock (`:391-426`), whose D3D handler
(`sub_829EEC48`) bumps the vblank counter, completes the queued swaps whose
target vblank has arrived and sets an event. The simulation loop
(`sub_823ED888` on `Guest 8255AE10`) ticks once per two vblanks
independently of rendering: 2.38 ticks per 19.9 ms frame, 1.00 per 8.33 ms
frame in the CSVs. The render thread (`Guest 825A6320`, loop
`sub_8259F3E8`) presents through `sub_829EFB30`: fence insert and ring
kick, `VdSwap`, then `sub_823E91F0` waits for the **previous** present's
fence, so the title runs at most one frame ahead of the GPU commands
thread, with a swap-queue throttle at 15 pending swaps; the fence predicate
`sub_829F04A8` is hooked to spin 20 us then block up to 1 ms on
`WaitForGpuWrite` (`src/native_renderer/graphics_hooks.cpp:105-147`). On
the GPU commands thread the ring-empty wait is 500 `SwitchToThread` calls
then a 5 ms event wait (`sdk/src/graphics/command_processor.cpp:246-266`),
and `WAIT_REG_MEM` yields up to 2 ms then `Sleep`s at least 1 ms
(`:1137-1145`). A race frame's draws take the thread about 14.6 ms at 1x
and it then waits 1.5-1.8 ms in `WAIT_REG_MEM` for the tick, which is why
frames land on 4, 5 or 6 vblanks and never between.

**The frame-rate ceiling is therefore the GPU commands thread and the
GPU, not the title.** The simulation costs 4.6-5.1 ms per tick (45 %
headroom at 120 Hz) and the render thread 6.5-7.8 ms per frame (6-22 %,
which is why PB-3 is not optional). No
pacing change is needed: the vblank stays at twice the target; on a 60 Hz
display `pinyon_shift_fh1_render_fps_limit = 120` with `host_present_fps_limit
= 60` shows every other source frame.

**The variable delta** (`sub_823ED888`, `.local/generated/default/pinyon_shift_recomp.254.cpp:458-712`):
elapsed time is read from a millisecond clock as an integer; 16 or 17 ms
snaps to 1/59.94 s with a residual accumulator; the only clamp is
`elapsed > 4000 ms -> 16`, and `f31 = max(f31, 0.0001)`. So a long frame
or a long fence wait is integrated as one big step (NP-4.10's "catching
up" at 4x), and at 120 fps the delta alternates 8 and 9 ms, a 6 % jitter
between ticks. The value is stored at `owner+448` (`0x823EDB84`), where
`PinyonShiftObserveSimulationDelta` already rewrites `f31` for the trainer.

| Item | Work | Expected | Size |
| --- | --- | --- | --- |
| PB-4.1 | **Deadline waits on the GPU commands thread**: the `Sleep(>= 1 ms)` in `WAIT_REG_MEM` and the 5 ms idle wait overshoot by 1-2 ms, 12-24 % of an 8.33 ms frame. The vblank deadline is known (`last_frame_time + interval_ticks`, `graphics_system.cpp:224`): wait on it with `SleepUntil`, or signal a host event from `MarkVblank` as `WaitForGpuWrite` does for fence words. | up to 2 ms of a 120 fps frame | S |
| PB-4.2 | **Host-measured simulation delta with a cap** in `PinyonShiftObserveSimulationDelta`: `steady_clock` between ticks at microsecond resolution, clamped to `[0.0001, pinyon_shift_max_simulation_step]` (default two target ticks, 0.0334 s), time scale applied after. Removes the millisecond jitter at 120 Hz and the catch-up burst in one place; the game slows under load instead of jumping. Gates: `expect-simulation-time 0.95 1.08`, the pose baseline and the save payload hash (the 16/17 ms snap disappears at a 60 fps limit). Optional deterministic mode: quantize to k x 1/120 with an accumulator. | correctness at 120 Hz and under load | S |
| PB-4.3 | **Per-frame animation steppers (NP-3.7)**: the crowd and the purchase animation run fast at high rates; `sub_82AE8AE0` multiplies the video mode's refresh rate by a constant into `obj+68` and is the first candidate for a refresh-derived step; once an updater is found, scale its increment by `delta / (1/30)` at the constant load. Reproducible by route (crowd) at 30 fps against unlocked, synchronized by game time. | correctness | M |
| PB-4.4 | **Thread placement for the measurement and the preset**: the busy host threads in a race are GPU Commands, the simulation loop, the render thread, one more guest thread and GPU Submission (2.9 cores at 60 fps); on the 8-core part the only risk is SMT-sibling pairing of GPU Commands with the simulation or render thread. Pin the three to distinct physical cores when measuring 120 fps (`latency_critical_thread_placement` exists, off by default) and adopt it in the preset if it holds p95. | p95 | S |
| PB-4.5 | **Global lock on the frame path**: the vblank interrupt runs guest code under the process-wide recursive mutex 7 times per 8.33 ms frame (2 vblanks plus 5 `INTERRUPT` packets), every guest event and wait takes it for a handle lookup (`sdk/src/system/xobject.cpp:370-449`), and `mtmsrd` takes it once per simulation tick. Measure the blocked time on the three critical threads (the `critical_region_contentions` column is a running total); then cache the object pointer in the dispatch header, stop holding the lock across the guest interrupt callback, and give `mtmsrd` a per-thread interrupt-mask flag where the title only masks interrupts. | unknown, likely 0.1-0.5 ms spread over threads | S to measure, M to fix |
| PB-4.6 | **Present**: neither backend uses host vsync (D3D12 `Present(0, ALLOW_TEARING)`, Vulkan immediate then mailbox); `host_present_fps_limit` paces presentation only and `pinyon_shift_fh1_source_presentation` presents each source frame once. At 120 fps on a 120 Hz display use mailbox (or the VRR path of NP-4.5) so every source frame reaches the display once; verify with `present_delta_ns` and `duplicate_present_count`. | delivery, not speed | S |

## PB-5 Gates and protocol

- **Speed.** Race window (PB-0.6 protocol, display at 120 Hz, window
  visible for the final check) median at or under 8.4 ms and p95 at or
  under 12.5 ms (three vblanks) at 3x on Vulkan; distinct presents at or
  above 110 per second on `fh1-race-sustained`; the same route at 1x and
  2x no slower than today.
- **Fidelity, bit-exact items** (transfers by copy, aliasing, descriptor
  and register-path work, codegen options that must be bit-identical):
  golden frame replays at 1x byte-identical or within the documented
  tolerance (`tools/test-fh1-frame-replays.py`).
- **Fidelity, accepted trades** (PB-1.1, stencil and transfer skipping):
  3x captures of `fh1-race-sync`, `fh1-free-roam`, `fh1-buy-car` and
  `fh1-map` compared by MAE against the faithful setting, and one look by
  the maintainer before the trade becomes a default.
- **Guest numerics** (PB-3): `expect-simulation-time 0.95 1.08`, the
  pose-drift gate on `fh1-timing-straight`, and `M5_TRACE save.file.write
  payload_hash` equality against control.
- **Never** the AppData save; seeds only.

## Working order

1. **PB-0** in full (about a week): Vulkan timing and counters, the 3x
   thread sample, the Nsight trace, the transfer-skip cvar, and the Vulkan
   baseline rows above.
2. **PB-1.1 first, with PB-1.8a and PB-1.8b right after** (small changes
   with a large share of pixel time), then **PB-1.3, PB-1.2, PB-1.4,
   PB-1.5 and PB-1.8c**: the GPU at 3x is the only stage that has no
   CPU-side workaround, and PB-1.1 is the largest single lever. Re-measure
   after each; expect the 3x GPU frame near 8 ms after these, with PB-1.7
   and PB-1.9 deciding the rest.
3. **PB-2 Tier 1** (PB-2.1 to PB-2.10) in parallel with step 2, since they
   touch different files: the GPU commands thread to about 10 ms, the race
   at 1x to about 90-100 fps.
4. **PB-4** pacing hooks and **PB-3** codegen, so the title's threads hold
   one simulation step per frame and the render thread keeps margin.
5. **PB-2.11** (decode-to-record), then **PB-2.12** if the recorder still
   misses 6 ms.
6. **PB-5** gates, then the 4K/120 preset in SETTINGS (3x, Vulkan, host
   MSAA mode 1 with FXAA, render limit 120).

Fallback tier if the GPU at 3x stays over budget after step 2: 2x with
FSR 1 or CAS to 4K, or the 2-sample host mode, both already settings.

## Relation to the native port backlog

NP-2 and NP-3's measured-and-deferred items, NP-9.1 to NP-9.5 and NP-15.1
are superseded by this backlog; their evidence stays where it is and each
row there now points here. NP-15.2 to NP-15.6 (Vulkan preparation, texture
fast paths, higher scales, the default switch, vendor qualification) stay
in the native port backlog and gate the switch of the default renderer, not
the frame rate.
