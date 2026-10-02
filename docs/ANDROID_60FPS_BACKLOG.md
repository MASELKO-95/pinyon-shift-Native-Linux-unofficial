# 60 fps at 1x on the Snapdragon 8 Elite

Goal: the race at 1x (1280x720 internal, MSAA off) at a sustained 60 fps on
the reference tablet (nubia NP05J, Snapdragon 8 Elite, Adreno 830, Qualcomm
driver), including after twenty minutes of play. 4x MSAA at 60 is out of
scope until the long-term item A60-11 exists.

This backlog comes from a read-only research pass on 2026-10-01 (its report
is not in the repository; its findings, code references and sources are
summarised here). It extends [ANDROID_PORT_BACKLOG.md](ANDROID_PORT_BACKLOG.md)
AP-7; items already tried there (Turnip, render-area trimming, lazy and
direct resolve fills, resolve aliasing, pass merging, scudo tuning) are not
proposed again.

## Where the frame goes

Measured, cold, MSAA off, busiest race section: 16.8 ms of GPU per frame, of
which the executor's own phases are at most 7.0 ms (transfers 2.0, resolves
2.0, clears 0.3, reloads after resolves 2.7). The frame opens about 280
renderings: about 93 are clears, each in its own full-surface rendering, 37
transfer batches, 75-85 are ended by resolves, 60-70 by texture reloads and
15 by shared-memory uploads; the title's own target changes are well under
50. Hot, the GPU span grows 28 % (22.3 to 28.6 ms median over 20 minutes) and
FH1's vblank pacing rounds frames up to 25 or 33 ms. To hold 60 hot, the cold
busiest frame needs to be about 12.5 ms: 4-5 ms of GPU savings plus less SoC
power so the clocks stay up.

Estimates to confirm: 20-40 us per rendering on this driver (5-11 ms a
frame), 0.8-1.2 GB of DRAM traffic per frame (likely the binding limit and the
main heat source), 4-8 ms of scene draws, 1-2 ms of present chain.

## Measurement protocol

`fh1-race-start-wait` from the `appdata-2026-09-27` seed with
`--fh1_msaa_single_sample=true --pinyon_shift_fh1_render_fps_limit=120`
(uncapped, so the frame time shows the work), A and B interleaved, the
tablet cooled to under 45 C CPU between runs. Compare by draw band
(frames with 4,000-7,000 draws are the busiest section): frame, GPU and
recorder medians, plus the executor's 600-frame rendering stats.

## Experiments

| ID | Experiment | Decides | Status |
| --- | --- | --- | --- |
| A60-E1 | Rendering-cost slope: a debug cvar ends the rendering every N draws; GPU span against renderings per frame | Per-rendering cost; ranks A60-1/2/3 against shader and bandwidth items | Open |
| A60-E2 | Cvar knock-outs: `anisotropic_override=-1`, `spirv_fast_pixel_math`, `vulkan_async_submission_split_draws` 256/2048, `fh1_native_stencil_export=false`, `gamma_render_target_as_unorm16=false`, `occlusion_query_enable=false`, `disable_motion_blur`/`disable_depth_of_field`, FIFO present, `wait_reg_mem_yield_us=0`; check `vsync` is true | A60-4b, A60-5, A60-6, A60-8, A60-10 | Open |
| A60-E2b | Composition check: `dumpsys SurfaceFlinger` while the game runs (CLIENT composition means the GPU rotates every frame) | A60-5.5 pre-rotation | Open |
| A60-E3 | Snapdragon Profiler trace (GMEM loads and stores per pass, bandwidth, GPU clock), cold and hot | Replaces the rendering and bandwidth estimates; needs the maintainer to install the profiler | Needs a person |
| A60-E4 | Thermal and clock log over 20 minutes | Which block throttles first | Done (AP-7.3) |
| A60-E5 | Present chain knock-out: bilinear instead of FSR 1 | Sizes A60-8 | Open |
| A60-E6 | Clear knock-out (wrong image): `ClearSurfaceRect` returns early | Upper bound for A60-1 | Open |
| A60-E7 | Reload-break knock-out: reloads issued right after the resolve | Lower bound for A60-2 | Open |
| A60-E8 | Sun shadow pass every other frame | The fidelity trade's value on Adreno | Open |
| A60-E9 | Thread placement on the 3.53 GHz cores, and sleep-only `WAIT_REG_MEM` | A60-5.1, A60-5.3 | Open |
| A60-E10 | Occlusion-query ends per frame | Whether A60-6 is worth anything | Open |

## Items

| ID | Item | Expected (cold ms) | Effort | Status |
| --- | --- | ---: | --- | --- |
| A60-0 | Correctness: `VulkanSharedMemory::GetUsageMasks(kComputeWrite)` declares `SHADER_READ` instead of `SHADER_WRITE` (`sdk/src/graphics/vulkan/shared_memory.cpp`), so a resolve's writes are not made available to the next reader; first suspect for AP-2.8's flicker. Also `k_2_10_10_10` targets hosted as 8-bit `A8B8G8R8` (`render_target_cache.cpp`) instead of `A2B10G10R10` | correctness | S | Open |
| A60-1 | Fold clears into the next rendering: a pending clear per surface, applied as `loadOp = CLEAR` when it covers the render area or `vkCmdClearAttachments` first inside the next draw rendering; applied as today before a transfer, resolve or sampling | 1.5-3.5 | M | Open |
| A60-2 | Eager reload at resolve time (in the compute phase the resolve opened), then untile straight into the image (`imageStore`, no scratch buffer and copy) | 1.0-2.5 | M | Open |
| A60-3 | GMEM-aware load and store ops from the executor's claim tracking: `LOAD_OP_DONT_CARE` after a full claim, `STORE_OP_NONE` for read-only depth, `STORE_OP_DONT_CARE` for bands cleared next (decoder lookahead) | 1.0-3.0 | L | Open |
| A60-4 | Translated shader cost: (a) implicit LOD with bias and no per-fetch `FDiv`; (b) `spirv_fast_pixel_math` on Android (and in the pack key); (c) `RelaxedPrecision` on pixel colour math; (d) `robustBufferAccess` off | 1.0-3.0 total | S-M each | Open |
| A60-5 | CPU power and pacing: (1) real waits instead of `sched_yield` spins (`WAIT_REG_MEM`, ring idle, vblank tail, `db16cyc`, alertable-wait tails); (2) ADPF performance hints and thermal headroom driving a quality ladder; (3) thread placement off the prime cores; (4) FIFO present; (5) pre-rotation | sustained fps | S each | Open |
| A60-6 | Occlusion-query ends wait only for their own submission instead of draining the queue | 0.5-2.0 if frequent | S-M | Open |
| A60-7 | Executor shader hygiene: no runtime integer division in transfer and resolve shaders, `spirv-opt -O`, single-sample variants that read non-MSAA images | 0.5-1.0 | S | Open |
| A60-8 | Present chain: sample the front-buffer surface directly, fold PWL gamma into the upscaler, SGSR 1 or bilinear instead of EASU + RCAS | 0.5-2.0 | S-M | Open |
| A60-9 | Sampler and texture bandwidth: the forced 4x anisotropy, `MUTABLE_FORMAT` on textures never sampled signed (defeats UBWC) | 0.5-1.5 | S | Open |
| A60-10 | Fidelity ladder hooks: motion blur and depth of field off, shadows every other frame, a 40 fps cap for even pacing when hot | 0.5-1.5 | S | Open |
| A60-11 | Long term: GMEM-resident resolves through `VK_KHR_dynamic_rendering_local_read`, then 4x MSAA with `VK_EXT_multisampled_render_to_single_sampled` | 2-4 | L-XL | Open |

Smaller: uploads before the rendering (PB-1.5 on a tiler, 0.3-0.8 ms);
submission split sizes; fragment shading rate on post-processing; 7e3 as
`B10G11R11` only if a census proves its alpha unread.

## Progress

Rows are added here as items are measured or done.

| Item | Status | Evidence |
| --- | --- | --- |
