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
| A60-E1 | Rendering-cost slope: a debug cvar ends the rendering every N draws; GPU span against renderings per frame | Per-rendering cost; ranks A60-1/2/3 against shader and bandwidth items | **Done.** A debug cvar `fh1_debug_rendering_split_draws` ends the draw rendering every N draws. Busiest band (5,000-7,000 draws): +268 renderings a frame cost +7.5 ms of GPU, +693 cost +16.4 ms: **24-28 us per rendering** on the Adreno 830 driver, inside the estimate |
| A60-E2 | Cvar knock-outs: `anisotropic_override=-1`, `spirv_fast_pixel_math`, `vulkan_async_submission_split_draws` 256/2048, `fh1_native_stencil_export=false`, `gamma_render_target_as_unorm16=false`, `occlusion_query_enable=false`, `disable_motion_blur`/`disable_depth_of_field`, FIFO present, `wait_reg_mem_yield_us=0`; check `vsync` is true | A60-4b, A60-5, A60-6, A60-8, A60-10 | **Done.** One run each against five interleaved baselines (busiest band frame 21.5-22.3 ms, GPU span 25.2-25.5 ms; noise about 0.5 ms): `anisotropic_override=-1` -0.6/-0.4 ms (frame/GPU), `spirv_fast_pixel_math` -0.3/-0.6, motion blur and depth of field off -0.6/-0.35, submission split 256 or 2048 nothing, `gamma_render_target_as_unorm16=false` nothing, FIFO nothing; `wait_reg_mem_yield_us=0` (millisecond sleeps) +2.5 ms. No knock-out is worth more than noise-level alone; `vsync` is true on the device |
| A60-E2b | Composition check: `dumpsys SurfaceFlinger` while the game runs (CLIENT composition means the GPU rotates every frame) | A60-5.5 pre-rotation | **Done.** `dumpsys SurfaceFlinger` with the game up: every layer DEVICE composition, `bufferTransform=0`, no client composition; the panel is 2400x1504 at ROTATION_0, so no GPU rotation pass exists and pre-rotation (A60-5.5) has nothing to gain |
| A60-E3 | Snapdragon Profiler trace (GMEM loads and stores per pass, bandwidth, GPU clock), cold and hot | Replaces the rendering and bandwidth estimates; needs the maintainer to install the profiler | Needs a person |
| A60-E4 | Thermal and clock log over 20 minutes | Which block throttles first | Done (AP-7.3) |
| A60-E5 | Present chain knock-out: bilinear instead of FSR 1 | Sizes A60-8 | **Done.** FSR 1 costs 1.4 ms (A60-8) |
| A60-E6 | Clear knock-out (wrong image): `ClearSurfaceRect` returns early | Upper bound for A60-1 | **Done.** `fh1_debug_skip_clears` (wrong image): 80 fewer renderings a frame and -2.0 ms of GPU (-1.0 to -1.8 ms frame) in the busiest band: the upper bound for A60-1 |
| A60-E7 | Reload-break knock-out: reloads issued right after the resolve | Lower bound for A60-2 | Covered by A60-2 |
| A60-E8 | Sun shadow pass every other frame | The fidelity trade's value on Adreno | Not built (see Progress) |
| A60-E9 | Thread placement on the 3.53 GHz cores, and sleep-only `WAIT_REG_MEM` | A60-5.1, A60-5.3 | Spins done; placement not built |
| A60-E10 | Occlusion-query ends per frame | Whether A60-6 is worth anything | **Done.** A counter of guest occlusion-query ends (`occlusion_query_end` in the 600-frame stats): 0 in the race, so A60-6 has nothing to gain |

## Items

| ID | Item | Expected (cold ms) | Effort | Status |
| --- | --- | ---: | --- | --- |
| A60-0 | Correctness: `VulkanSharedMemory::GetUsageMasks(kComputeWrite)` declares `SHADER_READ` instead of `SHADER_WRITE` (`sdk/src/graphics/vulkan/shared_memory.cpp`), so a resolve's writes are not made available to the next reader; first suspect for AP-2.8's flicker. Also `k_2_10_10_10` targets hosted as 8-bit `A8B8G8R8` (`render_target_cache.cpp`) instead of `A2B10G10R10` | correctness | S | Done |
| A60-1 | Fold clears into the next rendering: a pending clear per surface, applied as `loadOp = CLEAR` when it covers the render area or `vkCmdClearAttachments` first inside the next draw rendering; applied as today before a transfer, resolve or sampling | 1.5-3.5 | M | **Done** |
| A60-2 | Eager reload at resolve time (in the compute phase the resolve opened), then untile straight into the image (`imageStore`, no scratch buffer and copy) | 1.0-2.5 | M | Tried, dropped |
| A60-3 | GMEM-aware load and store ops from the executor's claim tracking: `LOAD_OP_DONT_CARE` after a full claim, `STORE_OP_NONE` for read-only depth, `STORE_OP_DONT_CARE` for bands cleared next (decoder lookahead) | 1.0-3.0 | L | Measured, dropped |
| A60-4 | Translated shader cost: (a) implicit LOD with bias and no per-fetch `FDiv`; (b) `spirv_fast_pixel_math` on Android (and in the pack key); (c) `RelaxedPrecision` on pixel colour math; (d) `robustBufferAccess` off | 1.0-3.0 total | S-M each | Option (4a); 4b-4d no gain or not built |
| A60-5 | CPU power and pacing: (1) real waits instead of `sched_yield` spins (`WAIT_REG_MEM`, ring idle, vblank tail, `db16cyc`, alertable-wait tails); (2) ADPF performance hints and thermal headroom driving a quality ladder; (3) thread placement off the prime cores; (4) FIFO present; (5) pre-rotation | sustained fps | S each | Spins done; ADPF measured, not shipped; 5.4, 5.5 not needed |
| A60-6 | Occlusion-query ends wait only for their own submission instead of draining the queue | 0.5-2.0 if frequent | S-M | Not needed |
| A60-7 | Executor shader hygiene: no runtime integer division in transfer and resolve shaders, `spirv-opt -O`, single-sample variants that read non-MSAA images | 0.5-1.0 | S | No gain |
| A60-8 | Present chain: sample the front-buffer surface directly, fold PWL gamma into the upscaler, SGSR 1 or bilinear instead of EASU + RCAS | 0.5-2.0 | S-M | **Done (presets)** |
| A60-9 | Sampler and texture bandwidth: the forced 4x anisotropy, `MUTABLE_FORMAT` on textures never sampled signed (defeats UBWC) | 0.5-1.5 | S | **Done (presets)** |
| A60-10 | Fidelity ladder hooks: motion blur and depth of field off, shadows every other frame, a 40 fps cap for even pacing when hot | 0.5-1.5 | S | Measured, not built |
| A60-11 | Long term: GMEM-resident resolves through `VK_KHR_dynamic_rendering_local_read`, then 4x MSAA with `VK_EXT_multisampled_render_to_single_sampled` | 2-4 | L-XL | Not started |

Smaller: uploads before the rendering (PB-1.5 on a tiler, 0.3-0.8 ms);
submission split sizes; fragment shading rate on post-processing; 7e3 as
`B10G11R11` only if a census proves its alpha unread.

## Progress

Rows are added here as items are measured or done.

| Item | Status | Evidence |
| --- | --- | --- |
| Method | Done | Single runs drift with the tablet's heat (the same build measured 21.5 to 32 ms in the busiest band across a batch), so settings that apply live are measured by an in-run A/B: a race route that alternates the setting every 300 frames for sixteen windows (`cvar` route steps), compared window by window under the same heat. Settings that need a restart are compared on the same route in interleaved runs |
| A60-0 barrier | Done | `kComputeWrite` declares shader writes; resolve writes are now made available to the texture loads and draws after them. Whether this is the AP-2.8 flicker needs a player's eye |
| A60-0 10-bit targets | Done | `k_2_10_10_10` targets are `A2B10G10R10_UNORM` (were 8-bit `A8B8G8R8`): same size, console precision |
| A60-1 fold clears | **Done** | Clears wait on their surface and are recorded inside the next draw rendering on it (or as its load op), in a rendering of their own only before anything else touches the image (`fh1_fold_clears`, on). 62.8 of 76 clears a frame fold, renderings drop from about 280 to 210. In-run A/B twice: 1.6 and 2.6 ms less frame time, 0.5 ms less GPU. Windows Vulkan captures unchanged by eye |
| A60-2 eager reloads | Tried, dropped | Textures learned which resolve invalidates them and reloaded right after it the next frame (in the resolve, then queued to the next draw's texture request). Both broke the image (HDR whiteout and yellow blowout from the first frames, the game then took another path), the queued form still ended as many renderings by reloads (68 a frame) and the first form reloaded 244 textures a frame against 86. Not worth more of its 1.5 ms ceiling |
| A60-5.1 spins | **Done (Android default)** | `wait_reg_mem_sleep_us` and `wait_reg_mem_yield_us` (100 and 100 on Android): in-run A/B, the commands thread's CPU time per frame fell 4.55 ms (a third) with frame time unchanged in 7 of 8 window pairs. `gpu_idle_spin_count` 20 instead of 500 changed nothing. Thread CPU time is now in the perf CSV on POSIX (`gpu_decoder_cpu_ns`, `gpu_recorder_cpu_ns`) |
| A60-8 FSR | **Done (presets)** | In-run A/B: FSR 1 to the 2400x1504 panel cost 1.4 ms of frame time (median pair) against bilinear. SMOOTH 60 now scales bilinearly |
| A60-9 anisotropy | **Done (presets)** | In-run A/B: the game's own filtering (`anisotropic_override=-1`) 0.43 ms faster than forced 4x, every pair. Both Android presets use it; ANISOTROPIC FILTERING gains GAME. The mutable-format change was not made: these textures already pass a two-format list (UNORM and SNORM of one layout), which lets the driver keep UBWC |
| A60-10 post-processing | Measured, not built | In-run A/B: motion blur and depth of field off changed nothing measurable (pairs within 0.5 ms either way); a thermal ladder of fidelity steps has little to switch now that both presets drop FSR |
| A60-5.5 pre-rotation | Not needed | E2b: the display hardware composes the game layer unrotated |
| A60-6 occlusion queries | Not needed | E10: the race ends no occlusion queries |
| A60-3 store and load ops | Measured; store side dropped, load side not built | Depth `STORE_OP_NONE` for draw renderings whose draws write neither depth nor stencil (the recorded begin patched before submission): 56 depth renderings a frame, only about 8 qualify (counted on the device; the in-run A/B of it was spoiled by an overlapping run), so a few depth stores a frame at most could be saved and it was taken out. Clears never become `LOAD_OP_CLEAR` because a surface's rendering area spans its whole EDRAM height while the game clears its 1280x720 band; that would need the area cut to the clear and a new rendering for any later draw outside it. Not built: the clears already fold in (A60-1) and the store side found little traffic to remove |
| A60-4a implicit LOD | Option, off | `spirv_implicit_lod_2d`: 2D fetches with computed LOD in pixel shaders sample with implicit LOD plus bias. GPU profile shares in two interleaved pairs: the scene's share of the frame fell 1 and 11 %, inside the run-to-run scene variation (each run parks the car in another place); kept as an option |
| A60-4b fast pixel math | Measured, no gain | Scene share unchanged or worse in two pairs; stays off |
| A60-4c, A60-4d precision, robustness | Not built | With implicit LOD and fast math inside noise, the translated pixel shaders' arithmetic is not what bounds the frame; RelaxedPrecision (banding risk in 7e3 targets) and robustBufferAccess off (GPU faults on bad fetch constants) are not worth their risk without a profiler showing ALU-bound passes |
| A60-7 executor shaders | Measured, no gain | `spirv-opt -O` shrank the executor's SPIR-V by 44 % but transfers and resolves took the same GPU time (6.94 and 6.96 ms a frame in the same pair): the Qualcomm compiler already does that work. Reverted |
| A60-E5 present chain | Done | FSR 1 1.4 ms of frame time (A60-8) |
| A60-E7 reload breaks | Covered by A60-2 | Both eager-reload forms were built and measured |
| A60-E8 shadows every other frame | Not built | The desktop measured and rejected the same trade (shadows trail by about half a metre at race speed for 2.5 % of the recorder) |
| A60-E9 spins and placement | Spins done (A60-5.1) | Thread placement not built: its only effect is power, which only the 20-minute soak can show, and ADPF (A60-5.2) leaves clock choice to the kernel |
| A60-5.2 ADPF | Measured, not shipped | A performance hint session for the commands, recorder and submission threads (target the frame interval, actual the recorder's CPU time per frame) and thermal headroom in the log. Two 20-minute SMOOTH 60 soaks from the same cooled start (skin 36 C), hint on and off, throttled at the same points within a tenth of the run (about 25, then 27, then 38 ms frames once the skin passes 44.5 C), so it was taken out |
| Sustained (AP-7.3 protocol) | Measured, environment-bound | Twenty minutes of `fh1-long-drive` at the new SMOOTH 60 settings (MSAA off, 60 cap, bilinear, the game's anisotropy) on the final build: 60 fps for the first minute, 43 at three minutes, 37-39 while the skin climbs from 40 to 44 C, then 26-28 fps once it passes 44.5 C (big cores at 2.0-2.2 GHz, GPU time 35.6 ms for the same scene that takes 20 ms cold). The tablet now cannot start below a 36 C skin (on AC power; the 2026-10-01 soak started at 29.6 C and its skin stayed under 41 C), so the soaks cannot compare builds; the per-frame savings above are the in-run A/B numbers. An old-build soak for comparison failed the route's thumbnail wait twice (the new build passed it in two of three runs), a flake before the drive starts. The thermal ceiling, not the frame cost, decides sustained 60: holding it needs about half the GPU's cold work, which only the long-term A60-11 restructuring could approach |
| A60-11 GMEM-resident resolves | Not started | The long-term item; renderings ended by compute resolves (about 76 a frame) and texture reloads (about 68) are now the largest remaining rendering breakers |
