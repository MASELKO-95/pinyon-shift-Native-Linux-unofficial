# Desktop renderer backlog: from PM4 interpreter to display-list compiler

Created 2026-10-02 at `dev` `47465e2` (ShiftGlue `91ecc6d`) from a research
pass of 2026-10-01 (its report is not in the repository; its findings,
code references and sources are summarised here). Long-term goal: get as
much performance as possible out of the desktop builds (Windows and Linux,
Vulkan default, D3D12 fallback). That means higher and steadier frame rates
at high resolution, low frame-time variance, low CPU cost, and headroom for
enhancements. It follows [PERFORMANCE_BACKLOG.md](PERFORMANCE_BACKLOG.md),
whose PB items are done, measured and dropped, or deferred; items here
name the PB items they extend.

## Where it stands

Reference machine: Ryzen 7 5800X, RTX 4080, Vulkan.

| Stage | 1x race | 3x race |
| --- | ---: | ---: |
| Frame | 8.33 ms median (120 fps), heavy stretch 9.1-9.2 ms | 22-24 ms, GPU-bound |
| GPU recorder thread | ~1.6 us per draw, ~8 ms for 5,100 draws, ~80 % busy: the 1x limiter | same work |
| GPU decoder thread | 40-60 % busy, mostly waiting in `WAIT_REG_MEM` on words the title's CPU writes | |
| Title render thread | 6.5-7.8 ms a frame, 68 % of it waiting on fences the recorder writes | |
| Simulation | 4.6-4.7 ms per step, one step a frame | |
| GPU | ~6.4 ms busy | ~22.5 ms: ~4.6 ms of per-draw front end plus ~1.9 ms per 1x of pixel area |

- **The heavy stretch is a latency chain, not a throughput deficit.** The
  title waits on fence words the recorder writes when it reaches them; the
  decoder waits on chunk-release words the title writes after those fences.
  A frame that misses 8.33 ms by a little costs a whole 4.17 ms vblank.
- **The recorder's 1.6 us per draw is 4-8x the host API's recording floor**
  (0.2-0.4 us per draw when every draw changes pipeline, descriptors and
  offsets). It is three orders of magnitude above re-executing pre-recorded
  command buffers (~1 ns per draw, NVIDIA's threaded CAD sample). No
  recorder subsystem is above 15 % of it.
- **FH1 renders from prebuilt display lists.** Only 42 of ~5,000 draws a
  frame go through the title's D3D emitter, 96 % of indirect buffers are
  byte-identical frame to frame, and 99.9 % of pointer-loaded constants
  repeat (PB-6). The lists come from a finite, inventoried set of title-side
  constructors and executors (the August lineage work, `93742f2`: six
  `PM4_INDIRECT_BUFFER` store sites, four owners, three producers, four
  context roots).

Changes from the Android work that apply here too (2026-10-02):
- EDRAM clears fold into the next draw rendering (`fh1_fold_clears`).
- Resolve writes are made available to their readers (barrier fix).
- 2_10_10_10 targets keep 10 bits.
- Thread CPU time is in the perf CSV (`gpu_decoder_cpu_ns`,
  `gpu_recorder_cpu_ns`).
Desktop keeps the spinning `WAIT_REG_MEM`, because Windows sleeps in
millisecond ticks.

## Decision: evolve the executor, do not replace it

How the other projects render, and why their seams do not fit FH1:

| Project | Seam | What it shows |
| --- | --- | --- |
| UnleashedRecomp, MarathonRecomp (hedge-dev) | D3D calls plus device dirty bits; XenosRecomp shaders offline | Lazy resolves (bind the render target until overwritten), bindless, async pipelines, present-wait pacing. FH1's per-draw D3D seam carries 42 draws a frame |
| skate3recomp 2.0 | Engine `RenderMesh`, ~27 hand-ported materials | Whole-frame native rendering in about three weeks for a simpler engine. FH1 has no batching to gain (the consecutive-run census found only single-draw runs) |
| Rayman Origins | Seven D3D hooks, no EDRAM | Works because that engine aliases nothing; FH1's wrappers saw only EDRAM copies |
| Xenia, ReXGlue default, re:Blue | PM4 emulation | What the executor descends from |
| RT64, Ship of Harkinian | Display-list interpretation | The closest model: FH1's natural seam is the **list** |

Both other seams were tried here and frozen:
- the skate3-style semantic capture (SNR) did not generalise across events;
- the ordered D3D stream saw only EDRAM copies.

So the executor stays the correctness baseline and grows a display-list
compiler, stage by stage:
1. identify each static indirect buffer and register its dependencies
   once;
2. replay its derived state;
3. then re-execute its recorded GPU commands.

Each stage keeps the interpreted path as a switchable twin. A same-frame
verify mode diffs the two. A compiled list is never trusted across a
dependency callback: it fails closed, counts and falls back.

**The honest 4K target.** At 3x the GPU does ~17 ms of pixel work that no
renderer design removes. The ALU, LOD and robustness levers measured as
noise (PB-1.8). The realistic tiers on an RTX 4080 are:
- **2x plus FSR 1 or CAS at 120 fps**;
- **3x at 90 fps**.

The pixel-side trades in DR-2 buy margin. A temporal upscaler is the
enhancement path.

## Verification rules

- **Judge final frames.** Golden replays must stay bit-exact for compiled
  paths, since a compiled path must reproduce the interpreted one.
  Fidelity trades are judged by MAE plus one look by the maintainer.
- **Skip with a reason and count it.** A compiled path that falls back
  says why, in counters.
- **Measure by the PB-0.6 protocol.** Three interleaved pairs, the busiest
  600 frames, and the vblank histogram, not only the median.
- **Two inconclusive trials on one gap mean re-rank,** not a third trial.
- **Seeds only.** Never the AppData save.

## Items

Effort: S under a week, M 1-3 weeks, L 1-2 months, XL longer. Gains are
estimates unless a measurement is cited.

### DR-0 Measure (1-2 weeks)

| ID | Item | Decides | Effort | Status |
| --- | --- | --- | --- | --- |
| DR-0.1 | **Nsight trace of a 3x race frame** (PB-0.4): ROP, shading and front-end split of the ~17 ms of pixel work | Which DR-2 trade leads; whether 3x is bandwidth-bound (DR-2.4) | S | **Done** (see Progress) |
| DR-0.2 | **Fence-chain counters**: hops per frame between title, decoder and recorder, and the wake latency of each (extend PB-0.3's packet census with timestamps) | Sizes DR-1.1 | S | **Done** (see Progress) |
| DR-0.3 | **Live indirect-buffer identity counters**: eligible IBs and draws per frame under DR-3.1's validity rules, without changing rendering | DR-3's hit rate before building it | S-M | **Done** (see Progress) |
| DR-0.4 | **One reverse-engineering session on the list executors**: confirm that `829F5FF0`/`829F6360` are the D3D runtime's command-buffer player and the `8240xxxx`-`8246xxxx` constructors the engine's per-object lists (an inference from address ranges today) | Whether DR-5.3 is possible | S | **Done** (see Progress) |
| DR-0.5 | **CPU reads of resolve ranges**: arm read traps on a seed route to find every resolve the CPU reads besides thumbnails | Whether DR-2.2 can drop the mirror write | S | **Done** (see Progress) |

### DR-1 Steady 120 at 1x (3-5 weeks)

Gate: `fh1-race-sync` race window median 8.33 ms **and p95 at most
8.4 ms** (two vblanks); at least 118 distinct presents a second.

| ID | Item | Expected | Effort | Status |
| --- | --- | --- | --- | --- |
| DR-1.1 | **Event-driven decoder wake.** **Measured, not adopted** (see Progress). A midasm hook at the title's store sites of the chunk-release words (`0x1FCA4000`-`17`) signals a host event that `WAIT_REG_MEM` waits on, bounded, as `WaitForGpuWrite` already does for the title side; and publish the record batch at once when the recorder's queue is empty instead of after 32 draws. PB-4.1 measured the sleep variant, not this | Heavy stretch 9.1 to ~8.5 ms; one vblank less at p95 on 10-20 % of frames | S-M | **Measured, not adopted** (see Progress) |
| DR-1.2 | **Fence completion at decode time** for fences whose preceding dynamic vertex and index ranges the decoder has already snapshotted, so the title starts its next chunk one recorder latency earlier | 0.3-0.6 ms of the tail | M-L (risk: a missed range corrupts geometry) | **Measured, not adopted** (see Progress) |
| DR-1.3 | **Vulkan shader packs at preparation** plus pipeline prewarm from the pack (Vulkan translates on a miss today; NP-15.2, AP-6.3) | No first-use hitches; lower p99 on first laps | S-M | **Done** (see Progress) |
| DR-1.4 | **Present pacing**: present-wait at the top of the guest frame, mailbox at 120 Hz without VRR, skate3's VRR cap of refresh minus max(4, 5 %) (PB-8.6, PB-4.6) | Even delivery (2.5 ms spread around 8.33 today) | S | **Implemented, needs the visible check** (see Progress) |
| DR-1.5 | **High-frame-rate correctness**: the crowd and purchase steppers (PB-9), with Unleashed's patterns | Correct speed at 120 fps | M | **Partly done** (see Progress) |

### DR-2 GPU headroom at 2x and 3x (6-10 weeks)

Gate: 2x race GPU at most 7 ms (the "2x plus FSR at 120" tier holds p95),
3x at most 11 ms ("3x at 90").

| ID | Item | Expected (3x) | Effort | Status |
| --- | --- | --- | --- | --- |
| DR-2.0 | **Texture loads without the copy engine** (from DR-0.1): write the untiled texels into the image from a compute shader through a raw-bits storage view instead of `vkCmdCopyBufferToImage` from the scratch buffer, for uncompressed single-level 2D textures | Most of the 13 ms texture-load time at 3x; some at 1x | M | **Done** (see Progress) |
| DR-2.1 | **Mixed resolution**: the shadow passes (the 1280-wide 1x sun depth, the 1040-pitch D24S8) and the half and quarter post chain at 1x while the scene renders at the chosen scale; needs a scale per surface and resolves between scales (PB-1.10) | ~1.9 ms for the sun shadow (knock-out) | L | Open |
| DR-2.2 | **Lazy resolve aliasing** (Unleashed's model, PB-1.2/PB-8.7): a resolve destination sampled before its source is redrawn binds the executor surface or a persistent native texture; the mirror write stays for CPU-read resolves; present from the native surface (PB-1.4). Note the Android lesson: reloading resolve-sourced textures early broke FH1's HDR chain (ANDROID_60FPS A60-2), so aliasing must track write generations exactly | ~2.6 + 0.3-0.6 ms (knock-outs) | L (high risk: 20 resolve kinds, several formats per address, exact `24_8` bits) | **Partly done** (see Progress) |
| DR-2.3 | **Transfers without nine passes on NVIDIA** (no `VK_EXT_shader_stencil_export`): copy the stencil byte from the words buffer into the stencil aspect with `vkCmdCopyBufferToImage` on single-sampled destinations; skip transfers a replay proves unread | ~1 ms of the at least 1.5 ms transfer floor | M | **Measured, not adopted** (see Progress) |
| DR-2.4 | **32bpp HDR main target trial**: host the 7e3 scene (`2_10_10_10_FLOAT`) as R11G11B10F plus a separate alpha strategy if any pass reads its 2 bits, instead of RGBA16F | Unknown; possibly the largest 3x lever if ROP or bandwidth bound | S-M to try | **Not needed**: DR-0.1 measured ROP at 3 % and DRAM at 17 % of peak over the 3x frame, so halving the scene's colour bandwidth has nothing to gain |

### DR-3 The display-list compiler, CPU side (8-12 weeks)

Gate: recorder at most 5 ms per 5,100-draw race frame; at least 50 % of
draws eligible; zero verify mismatches across the route matrix.

| ID | Item | Expected | Effort | Status |
| --- | --- | --- | --- | --- |
| DR-3.1 | **Identity and dependencies.** Identify each IB by physical address, dword count and constructor generation (the six known store sites), kept valid by a write watch on its pages; run the PB-6.1 state hash at IB entry; register the IB's dependencies once and invalidate them by callbacks (write watches, texture outdated marks, tile generations) rather than per-draw tests. Dependencies are texture keys and generations, vertex and index ranges, `LOAD_ALU_CONSTANT` source pages, and executor tile ownership at entry. This answers PB-6.4's "the validity tests are today's costs" | Enabler; the decoder stops re-parsing 1-1.8 M dwords of valid IBs a frame | M-L | Open |
| DR-3.2 | **Recorder templates.** For a valid IB whose entry hash matches and whose dependencies are clean, replay last frame's derived state per draw (pipeline, translations, viewport, bindings, target keys), gathering only float constants and rewriting dynamic offsets; anything else falls back. With `fh1_compiled_ib_verify` running both paths on a sample of IBs and diffing the derived state | ~0.4 instead of 1.6 us per eligible draw; recorder 8 to ~5-6 ms per race frame | L | Open, after DR-3.1 |
| DR-3.3 | **A synthetic invalidation test**: a route that rewrites an IB's constants and textures between frames and asserts the compiled path invalidates | Correctness gate for DR-3 | S | Open |

### DR-4 New binding ABI and GPU-side re-execution (10-16 weeks)

Gate: recorder at most 3 ms; 2x at 120 with p95 within one vblank; no
regression on RADV.

| ID | Item | Expected | Effort | Status |
| --- | --- | --- | --- | --- |
| DR-4.1 | **Bindless textures and constants by address**: descriptor indexing from a small per-draw block, float constants read through a buffer device address, system constants split into pass-level and per-draw. A new pack version, v3 packs still loadable for A/B (PB-7.2, PB-7.3, PB-2.12) | 3-7 % of the recorder alone; required so compiled GPU work survives texture re-creation and constant changes | L | Open |
| DR-4.2 | **GPU-side re-execution**: record each compiled IB segment (split at target changes, transfers, resolves and CPU-visible packets) into a secondary command buffer (D3D12: bundle) whose draws take constants and descriptors through per-frame tables indexed by a baked draw id; re-execute instead of re-recording. AMD advises against secondary buffers for GPU time, so keep DR-3.2 as the AMD default until measured on RADV | Recorder to ~2-3 ms; most of a core freed for the title | L-XL | Open, after DR-3.2 and DR-4.1 |

### DR-5 Margin and enhancements (ongoing)

| ID | Item | Expected | Effort | Status |
| --- | --- | --- | --- | --- |
| DR-5.1 | **Register-locality codegen**: `non_volatile_as_local`, `cr_as_local`, offset-free stack and TLS accesses, MXCSR propagation, fused `vmaddfp` on the FMA baseline (off today; PB-3.2-3.5, 3.8), gated by pose drift and save hash | 15-30 % of guest CPU; the render thread's margin at 120 Hz and the only lever for 144/165 Hz and slower CPUs | L | Open |
| DR-5.2 | **Simulation cadence apart from the vblank**: count host frames in the "two vblanks per step" logic so the vblank can run finer without speeding the simulation; frames then quantise at 2.08 ms instead of 4.17 ms (needs `sub_829EEC48`/swap completion reversed) | A 9 ms frame costs 10.4 ms, not 12.5 | M-L | Open |
| DR-5.3 | **Hook the title's list executors** to feed compiled lists directly, so static lists are never parsed as PM4 | Decoder work proportional to lists, not dwords | XL | Open, after DR-0.4 and DR-3 |
| DR-5.4 | **Temporal upscaling or frame generation** (FSR 3.1): depth from the executor, camera motion vectors reconstructed from depth and the view-projection constants, HUD-less colour from the pre-HUD front buffer | Perceived 4K at 120 from 2x at 60-90; ghosting on cars without per-object vectors | L-XL (research first) | Open, after DR-2.2 |
| DR-5.5 | Translator: structured control flow for the 372 jumpy pixel shaders; the scale as a specialization constant (one pack for every scale) | Low on this GPU (PB-1.8a/b noise); packaging and live scale switches | M, S-M | **Not needed** (see Progress) |

## Not to build

- **A D3D-call renderer**: the census leaves it 42 draws a frame.
- **A skate3-style hand-ported scene renderer**: months of material work,
  no draw-count gain. It stays the fallback if DR-3's hit rate disappoints.
- **A general RHI.**
- **D3D12 parity work** beyond keeping it the fallback.
- **Ideas already measured and dropped:**
  - MMCSS and priority boosts, PGO and AVX2 plugin builds;
  - 200 us `WAIT_REG_MEM` sleeps on Windows;
  - constant coalescing and compare-before-upload;
  - submission splits;
  - a persisted `VkPipelineCache` on NVIDIA;
  - barrier elision at 3x;
  - fast pixel math, implicit LOD and robustness off at 3x;
  - the 1x shadow pass via blits;
  - band merge;
  - frame interpolation for high frame rates.

## Risks and open questions

| Risk | Settled by |
| --- | --- |
| The list-executor identification is an inference | Settled by DR-0.4: `82416A00` (`D3DDevice_RunCommandBuffer`) is the seam; DR-1 to DR-4 still key on PM4 identity and do not depend on it |
| IBs the census never saw (night, weather, livery editor, multiplayer) or changed in place | IB page write watches in DR-3.1, fail closed; routes for night and weather before DR-3 ships |
| `LOAD_ALU_CONSTANT` sources that change (0.1 % in the race) | Watch their pages like vertex data; count triggers |
| Late or over-wide invalidation (256 KiB speculative watches) | The verify mode and hit-rate counters; narrow only when measured safe |
| Too many segments per IB for re-execution | DR-0.3's per-IB packet census; re-execute only IBs with few segments |
| No AMD or Intel data; NVIDIA lacks stencil export | Keep the interpreted path the default where unmeasured |
| The 3x frame is unsplit | DR-0.1 before any DR-2 work beyond DR-2.1 |
| The binding ABI change touches every pack and the goldens | Behind a pack version; re-record goldens once with both ABIs bit-exact |
| Once the recorder is fast, the title's render thread (6.5-7.8 of 8.33 ms) limits | DR-5.1 |
| Vblank quantisation makes small misses expensive | DR-1.1 reduces lateness, DR-5.2 halves the step |

## Progress

| Item | Status | Evidence |
| --- | --- | --- |
| DR-5.5 translator and per-scale packs | Not needed (2026-10-02) | Structured control flow speeds up shading, and the 3x frame is not shading-bound: GPU Trace (DR-0.1) has the SMs 20 % of peak over the frame, and PB-1.8's shader trials (fast pixel math, implicit LOD) stayed within 1 %. One pack for every scale serves the Direct3D 12 packs; the Vulkan default builds no pack (it translates the stored microcode for the chosen scale while starting, DR-1.3), so it has no consumer. Revisit if a profile shows pixel shading leading |
| DR-2.1 unclipped draw extents (transfers) | Done as a step (2026-10-02) | The one-frame operation log showed a stencil-only draw on the 4x depth surface claiming all 2,048 EDRAM tiles: a draw with clipping disabled and the D3D9 default 8192 scissor had no extent estimate, so it took every tile from its base, moving the 1x depth and two colour surfaces into it with stencil, and they moved back when drawn again. Upstream Xenia runs such draws' vertex shader on the CPU to bound them by default; this port had `execute_unclipped_draw_vs_on_cpu` off since the import, and it is now on (SDK `7e4835c`). In-run A/Bs: 3x 1.27 ms less GPU and 0.83 ms less frame time (all 8 pairs); 1x heavy race start equal or better in matched pairs. Profiled after it: 3x executor GPU frame 8.8-9.7 ms (transfers 1.7-2.4 ms, from 3.1-3.3), 2x 8.5-8.8 ms at the 120 limit (transfers 1.1-1.3 ms, from 1.7-1.85). The 1x/4x aliasing of the main depth buffer (above) still transfers on every switch |
| DR-2.2 resolve into the texture (second step) | Partly done (2026-10-02) | For a 32bpp colour resolve (`8_8_8_8`, `2_10_10_10`, `32_FLOAT`), the executor asks the texture cache (now indexed by base address) for a current texture of the same format, pitch and scaling whose base level holds the written range, with the destination a whole number of macro tile rows below its top, and the resolve's dispatch also stores each word into that texture's raw-bits view, swapped by its endianness, which is exactly what its reload would leave; after the range's invalidation the texture is marked current again (SDK `79090f0`, `fh1_resolve_to_textures`). About 12 resolves a frame on the 3x race update their texture in place. A first form ran a second resolve for the texture and cost 1.4 ms more resolve time than the 0.75 ms of reloads it saved; one dispatch writing both costs only the image store. In-run A/B at 3x on top of band reloads: 0.74 ms less GPU and 0.36 ms less frame time, all 8 pairs; profiled 3x GPU frame 10.2-10.3 ms (texture reloads 1.15-1.27 ms, from 1.8-2.25 before both steps). Captures clean at 1x and 3x. **Depth too** (SDK `b195361`): a depth resolve (the `24_8` word) updates a current `24_8` or `24_8_FLOAT` texture, converted as their loads do (unorm `(d + (d >> 23)) * 2^-24`, 20e4 through `Float20e4To32`, read from the load shaders' SPIR-V); about 16 resolves a frame then update their texture in place, and the in-run A/B of the whole feature against none is 0.96 ms less GPU a 3x frame (all 8 pairs); profiled 3x GPU frame 9.9-10.0 ms, texture reloads 0.82-0.92 ms; shadows checked by eye. Still open: dropping the mirror write where the CPU never reads (DR-0.5) |
| DR-2.2 band reloads (first step) | Partly done (2026-10-02) | A census of the resolve-sourced reloads at 3x (about 57 a frame): the 1280x720 `2_10_10_10` scene texture at `0x1C4E1000` reloads about five times a frame (FH1 resolves it in three tile bands, 256, 256 and 208 rows, and samples it between them), two 1280x720 `8_8_8_8` and a 640x360 one about twice each, three 1024x1024 `24_8` shadow depths, the 1280x720 `24_8_FLOAT` depth, and a long tail of small `2_10_10_10` bloom and luminance levels, each a full untile of the whole texture (33 MB for a frame-sized one at 3x). **Band reloads** (SDK `5f6a3d9`, `texture_band_reloads`): the texture cache logs every watched write range in order, and a base-only reload of a single-level tiled 2D texture whose overlapping writes since its last load were all GPU writes untiles only the 32-row macro tile rows they touched (contiguous in the tiled layout) and copies them from that row; a CPU write or an expired log position reloads the whole texture, so the data is exactly what a full reload gives. About 4 of the 57 reloads a frame become band reloads; in-run A/B at 3x: 0.64 ms less GPU and 0.63 ms less frame time, all 8 pairs; captures clean at 1x and 3x. Still open: resolving straight into a matching texture (no untile at all) and dropping the mirror write for ranges the CPU never reads (DR-0.5) |
| DR-1.5 high-frame-rate steppers | Partly done (2026-10-02) | A static pass over the generated code (report kept out of tree) found the steppers that advance by a constant per update. In the port the guest vblank runs at twice the render limit and the simulation ticks once per two vblanks, so any constant step runs limit/30 times its speed at the 30 fps limit. **Fixed**: `CUI4AnimatedCamera::Update` (`sub_82649960`), which drives the car purchase and reveal shots, adds 1/30 s per update; a mid-assembly hook at `0x826499C8` replaces it with the real time since that camera's previous update (`pinyon_shift_fixed_steps_real_time`, on). On `fh1-buy-car` at the 30 limit: 30 updates a second of exactly 1/30, applied 1.000 s a second against the constant's 1.001; unlocked, the showroom shot ran 0.41 s of real time where the constant advanced it 1.5 s (3.6 times), and the hook advances it 0.41 s. **Found, not hooked** (no route reaches them to verify the register each holds): the showroom idle orbit (`sub_82653300`, 0.1 degree per update at `0x8265354C`, `f0`), `CAnimatedModelPresentation::Update` (`sub_82DD2B78`, 1/60 s at `0x82DD2B84`, `r3`/`f1`), the radio popup timer (`sub_82801218`, 1/30 at `0x82801384`, `f13`); the same helper takes each with one line once a route shows it firing. **The crowd is not explained yet**: `CCrowdPresentation::Update` (`sub_82DE2130`) already advances by the simulation delta in 25 ms frames, and a probe at its entry was never called on `fh1-buy-car`, `fh1-race-start-wait` or `fh1-race-sync`, so the crowds those scenes show are another presentation; next is a census of the presentation updates that run in a crowd scene at limits 30 and 120 |
| DR-1.4 present pacing | Implemented, needs the visible check (2026-10-02) | `vulkan_present_wait` (SDK `d640e0b`, off by default, hot reload) enables `VK_KHR_present_id` and `VK_KHR_present_wait` (both on the RTX 4080 driver 581.08) and keeps one present in flight: each present carries an ID and the presenter waits, up to 50 ms, for the previous one to reach the display before painting the next. A hidden race run with it on passes. Still for the maintainer at the 120 Hz display: compare `present_delta_ns` and `duplicate_present_count` and the feel with it on and off, with `vulkan_allow_present_mode_immediate = false` (mailbox) without VRR, and with VRR; then decide the default and skate3's VRR cap (refresh minus max(4, 5 %)) |
| DR-2.1 transfer census (analysis) | Open, evidence (2026-10-02) | `fh1_native_gpu_profile` at 3x on the race: the executor's GPU frame is 11-12.3 ms, of which transfers 2.7-3.5 ms (the largest share), texture reloads 1.8-2.2 ms, resolves 1.6-1.7 ms; GPU Trace: depth transfers stall the front end on wait-for-idle 96 % of their time and colour transfers 99 %, so their cost is barriers, not pixels. A one-frame operation log shows the largest single source: FH1 aliases its 1280x720 1x depth-stencil (EDRAM base 0, pitch 16) as a 640x360 4x MSAA depth (the console trick for quarter-resolution transparencies tested against full-resolution depth: the two layouts share samples), and the frame switches between them four to five times, each a 720-tile transfer with stencil, about a third of all transfer tile-passes. The executor hosts the two as separate images (the 4x one single-sampled at scale), so every switch copies. Removing it needs both layouts on one host image (the 4x pass rendered into the 1x image at twice the viewport scale, or the 1x image read as the 4x one), which is this item's per-surface scale work |
| DR-1.3 Vulkan shader preparation | **Done (2026-10-02)** | The Vulkan backend already keeps what it creates (`.xsh` shader and `.vk.xpso` pipeline storage, the `VkPipelineCache`) and recreates stored pipelines while the game starts, and NVIDIA's driver keeps its own shader cache; render-test seeds carry none of them. Measured on `fh1-race-start-wait` at 1x: with the storage and the driver cache, 1.1 s of frame time over 16.7 ms; without our storage but with the driver cache, 1.5 s; **with an empty driver cache too (`__GL_SHADER_DISK_CACHE_PATH` on an empty folder), as on a first launch, 13.0 s, single frames up to 1.35 s, 37 frames over 100 ms, many inside the race**. `prepare-fh1-shaders.ps1` now runs `prepare-fh1-vulkan.ps1` when the install has no Vulkan pipeline storage: it plays the shader preparation route once, hidden, with the player's settings (125 s here), which fills the driver cache, and keeps the storage it wrote without replacing any the player has; a failure is a warning, not retried until the build changes. Same route with an empty driver cache after preparation: 5.7 s, 25 frames over 100 ms, at most 0.53 s; what remains is content the opening does not reach (this save's race, car and menus). A SPIR-V pack built from the disc corpus would cover more shaders but not pipelines, which are what the driver compiles, so it is not built |
| DR-1.2 decode-time completion | Measured, not adopted (2026-10-02) | The decoder's 12 WAIT_REG_MEMs a frame are D3D's GPU-to-CPU callback handshake (InsertCallback, `sub_823E67A8`): WAIT_UNTIL 3D idle, SCRATCH_REG4 = callback, REG5 = context, REG0 = CPU mask (written back to `0x1FCA4000`), waits for those write-backs, INTERRUPT, a wait for the handler (`sub_829EE1F8`) to clear the mask, REG4 = `0x0BADF00D`. Two callbacks run: **`0x829F5948`**, D3D's predicated-tiling step (queues the next tile's replay for the worker `sub_829F6620`, which writes the tile's indirect buffers to the ring; a 4-entry queue per hardware thread), and **`0x82586398`**, the engine's GPU frame-end timestamp. Static analysis (report kept out of tree) found neither releasing memory earlier draws read, so both can run when the decoder reaches them, as the console's command processor does. Built as `gpu_decode_time_scratch_writeback_mask` and `gpu_decode_time_interrupt_callbacks` and measured in-run at 1x on the race start in 100-frame windows (all 6,000-7,400 draws): the decoder's waits fall from 12 to 3 a frame, but **frame time does not move (11.04 against 11.03 ms)**: in heavy frames the recorder and GPU bound the frame (frame time equals the guest GPU time), so the decoder's waits only move. An earlier 300-frame A/B that suggested 2.7 ms had its heavy windows on one side. Patches kept out of tree; the lever for heavy 1x frames is the recorder (DR-3) |
| DR-2.3 stencil transfers on NVIDIA | Measured, not adopted (2026-10-02) | Debug labels now split transfer and clear renderings by depth and color (SDK `ac50b2d`). GPU Trace of the 3x frame after DR-2.0 (19.4 ms): draws 6.3 ms, texture loads 3.7, resolves 2.8, **depth transfers and clears 2.5 ms over 23 renderings with the front end stalled on wait-for-idle 91 % of it**, transfer words 1.3, color transfers and clears 1.0. In-run A/Bs on the 3x race (`fh1_debug_skip_stencil_transfers`, hot reload): dropping the eight stencil-bit passes saved 5.6 ms of GPU time with the desktop compositor loading the GPU and 1.0-2.5 ms with it lighter; with the desktop idle the passes cost about 1.25 ms. Two replacements were built and measured against them in the same runs, and dropped: (1) **the stencil bytes copied into the stencil aspect** (a compute pass packing the words' low bytes after them in the words buffer, then `vkCmdCopyBufferToImage` on single-sampled destinations made transfer destinations) cost 0.2 ms more GPU than the passes and 1.5 ms more than none: the copy ends the rendering and moves the depth image to `TRANSFER_DST` and back, and those barriers cost more than the fragment work of eight passes; (2) **one pass per known stencil value** (values tracked per surface through clears, draw stencil ops with the test modelled to a fixed point, and transfers, with tiles made unknown by color sources) applied to 15 % of batches, because tiles that colour surfaces wrote carry any value, and changed nothing measurable. Transfer cost here is barrier-bound, so the lever is fewer transfer batches (DR-2.2), not cheaper stencil writes. The unread-transfer skip moves to DR-2.2, which needs the same read tracking. Patches kept out of tree |
| DR-0.4 list executors | **Done (2026-10-02)**, hypothesis partly refuted | One reverse-engineering session. `829F5FF0`/`829F6360` are D3D runtime code, not the engine: on D3D's worker threads they replay D3D's tiling stream into the ring once per tile, and each indirect buffer they emit covers a whole D3D segment, not one object's list. The `8240xxxx`-`8246xxxx` functions are D3D as well: **`82416A00` is `D3DDevice_RunCommandBuffer`, the per-object list executor**, `82409398` writes the ring and moves its write pointer, `8240CF68`/`8240D070` close and submit segments; the engine side is `824167F8` and `82417060`, reached from `CProceduralModels` slot 41 (`82417BC0`) and `824365B0`. A list is an `IDirect3DCommandBuffer` (resource type 9): `+116` chains (size, physical address) pairs, one indirect buffer each; `+24..+56` mark inherited and `+64..+96` overwritten state, `+108` flags, `+112` dependent resources; nothing patches a list when it runs. Per-frame state goes into the calling segment just before the indirect-buffer packet (float constants by `824168B0` as register writes), tiling adds per-tile predication, and the only `LOAD_ALU_CONSTANT` emitter (`82411BD0`) loads static shader tables, which explains DR-0.3's repeat rate. So DR-5.3 hooks `82416A00`'s entry, not the worker threads, and DR-3.1 keys lists on the command-buffer object plus a recording count, with tile and predication state in the entry hash so a list cached for one tile is not replayed for another |
| DR-2.0 compute texture copies | **Done (2026-10-02)** | Textures of at least 256x256 texels (scaled size), single-level 2D, in 4-, 8- or 16-byte uncompressed formats are created storage-capable (mutable format, extended usage) with an R32/RG32/RGBA32_UINT view, and `texture_copy_buffer_image.comp` writes the untiled words into them (`vulkan_texture_load_compute_copy`, on for desktop, off on Android where storage images may lose UBWC). GPU Trace of the same 3x frame: texture loads 13.1 to 3.9 ms, the frame 27.8 to 19.1 ms. Interleaved 3x race runs (`fh1-race-start-wait`, frames with at least 4,000 draws, 120 cap): frame median 24.7 and 25.2 ms against 39.9 and 41.0 ms, p95 39-42 against 62 ms, GPU median 25 against 39 ms. 1x unchanged (11.5-12.2 ms either way). Captures correct by eye at 1x and 3x. Two things seen on the way, not caused by it: the 3x race without it measured 40-42 ms where PB recorded 22-24 ms on 2026-09-30, and the pre-session SDK (`88e1cbb`) measures the same 42.0 ms on `fh1-race-sync`, so it is not a code regression: on 2026-10-02 the GPU ran at 48-67 % utilization with the game closed (the desktop compositor across the machine's virtual displays), so only interleaved A/Bs from that day compare; and the free-roam scene before the event shows posterized colour at 3x with or without it (and with 2_10_10_10 at 8 bits), while 1x is smooth |
| DR-0.5 CPU reads of resolves | **Done (2026-10-02)** | `fh1_trace_resolve_cpu_accesses` arms every resolved range (117 distinct ranges in `fh1-race-sync`, 154 in `fh1-buy-car`) for access callbacks after each resolve. No guest CPU read of a resolved range in either route. The only accesses are writes reusing resolved pages around `0x1D822000`-`0x1D827000` (link registers `8314CC98` and, in the purchase, `82C7508C`). Thumbnails reach files through host-side reads, which the existing one-off readback covers. So DR-2.2 can keep resolve output on the GPU and drop the mirror write except for one-off resolves, provided CPU overwrites keep invalidating |
| DR-0.3 list identity | **Done (2026-10-02)** | `gpu_ib_identity_stats` hashes every indirect buffer at execution and compares it with the previous execution at the same address and size. 1x `fh1-race-sync`, 600-frame windows in the race: 680-1,735 buffers a frame, 96.2-97.9 % repeated byte for byte, holding 67-77 % of the decoded dwords and 49-58 % of the draws (nested buffers count in both, so draws are over-counted alike). The buffers that change are few but large: the compiler's ceiling is about half the draws, as the research estimated (45-65 %) |
| DR-0.2 wait chain | **Done (2026-10-02)** | New perf CSV columns `gpu_reg_mem_wait_count`, `gpu_reg_mem_slept_count`, `title_gpu_fence_wait_count`, `title_gpu_fence_wait_ns`, and the `gpu_trace_wait_reg_mem_writers` tracer. 1x `fh1-race-sync`, last 600 frames: the commands thread waits in WAIT_REG_MEM 12 times a frame for 5.4-6.2 ms in all, and the title's render thread waits on GPU fences 10-11 times a frame for 5.5-6.3 ms. Every wait is an equality wait on one of two words, physical `0x1FCA4000` (values toggling 0 and 4) and `0x1FCA4004` (0 and 1): the GPU scratch register write-back area (`SCRATCH_ADDR`). Guest CPU code writes only the second, from the title's interrupt callback `sub_829EEC48` (link register `829EEC64`, thread `F800001C`); the first is written only by the recorder's scratch register write-backs, which are host stores. Thread CPU time columns are useless on Windows: `GetThreadTimes` counts in 15.6 ms ticks |
| DR-1.1 event-driven wake | Measured, not adopted (2026-10-02) | WAIT_REG_MEM blocked on the existing GPU-write signal (raised by the recorder's guest writes, and added after the guest vblank callback and the scratch write-backs) with a 1 ms timeout, instead of yielding: in-run A/B at 1x, 21.5 ms frames against 8.8 ms, and 18.8 against 13.4 ms after the scratch signal was added, because 4 of the 12 waits a frame still ended on the timeout (another unsignalled release) and Windows waits in millisecond ticks. The yield loop already sees a release within microseconds: the decoder's waits cost CPU, not latency, so there is no latency for a wake to remove. Taken out; the tracer stays |
| DR-0.1 Nsight GPU Trace | **Done (2026-10-02)** | Nsight Graphics 2026.3.1 GPU Trace of one 3x race frame (route frame 4380 of `fh1-race-start-wait`, single-sampled MSAA), launched through `launch-preview.ps1 -NsightCommand` (`run-fh1-render-test.py --nsight-gpu-trace`) with GPU performance counters allowed for all users. Passes labelled with `vulkan_debug_labels`. GPU frame 27.8 ms under trace. **The frame is neither ROP- nor bandwidth-bound:** over the frame the graphics engine is active 60 %, SMs 20 %, DRAM 17 %, ROP 3 %, depth ROP 2 %, and the front end stalls on wait-for-idle 35 % of the time. By label: **texture loads 13.1 ms (90 loads; the ten largest 0.4-0.8 ms each, graphics engine active only 15 % during them, the copy engine busy 40 % of the frame)**, draw renderings 5.6 ms (190), transfers and clears 4.7 ms (79), resolves 2.6 ms (96), uploads 0.1 ms, gaps 1.7 ms. The resolve-sourced reloads at 3x are scaled, frame-sized textures whose `vkCmdCopyBufferToImage` from the untile scratch buffer runs on the copy engine serialized with the frame. DR-2.0 is new and leads DR-2; DR-2.4 (32bpp HDR) has nothing to gain at 3 % ROP |
