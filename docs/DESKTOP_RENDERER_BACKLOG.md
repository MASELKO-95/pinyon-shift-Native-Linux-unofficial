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
| DR-0.2 | **Fence-chain counters**: hops per frame between title, decoder and recorder, and the wake latency of each (extend PB-0.3's packet census with timestamps) | Sizes DR-1.1 | S | Open |
| DR-0.3 | **Live indirect-buffer identity counters**: eligible IBs and draws per frame under DR-3.1's validity rules, without changing rendering | DR-3's hit rate before building it | S-M | Open |
| DR-0.4 | **One reverse-engineering session on the list executors**: confirm that `829F5FF0`/`829F6360` are the D3D runtime's command-buffer player and the `8240xxxx`-`8246xxxx` constructors the engine's per-object lists (an inference from address ranges today) | Whether DR-5.3 is possible | S | Open |
| DR-0.5 | **CPU reads of resolve ranges**: arm read traps on a seed route to find every resolve the CPU reads besides thumbnails | Whether DR-2.2 can drop the mirror write | S | Open |

### DR-1 Steady 120 at 1x (3-5 weeks)

Gate: `fh1-race-sync` race window median 8.33 ms **and p95 at most
8.4 ms** (two vblanks); at least 118 distinct presents a second.

| ID | Item | Expected | Effort | Status |
| --- | --- | --- | --- | --- |
| DR-1.1 | **Event-driven decoder wake.** A midasm hook at the title's store sites of the chunk-release words (`0x1FCA4000`-`17`) signals a host event that `WAIT_REG_MEM` waits on, bounded, as `WaitForGpuWrite` already does for the title side; and publish the record batch at once when the recorder's queue is empty instead of after 32 draws. PB-4.1 measured the sleep variant, not this | Heavy stretch 9.1 to ~8.5 ms; one vblank less at p95 on 10-20 % of frames | S-M | Open |
| DR-1.2 | **Fence completion at decode time** for fences whose preceding dynamic vertex and index ranges the decoder has already snapshotted, so the title starts its next chunk one recorder latency earlier | 0.3-0.6 ms of the tail | M-L (risk: a missed range corrupts geometry) | Open, after DR-1.1 |
| DR-1.3 | **Vulkan shader packs at preparation** plus pipeline prewarm from the pack (Vulkan translates on a miss today; NP-15.2, AP-6.3) | No first-use hitches; lower p99 on first laps | S-M | Open |
| DR-1.4 | **Present pacing**: present-wait at the top of the guest frame, mailbox at 120 Hz without VRR, skate3's VRR cap of refresh minus max(4, 5 %) (PB-8.6, PB-4.6) | Even delivery (2.5 ms spread around 8.33 today) | S | Needs a person at the 120 Hz display |
| DR-1.5 | **High-frame-rate correctness**: the crowd and purchase steppers (PB-9), with Unleashed's patterns | Correct speed at 120 fps | M | Open |

### DR-2 GPU headroom at 2x and 3x (6-10 weeks)

Gate: 2x race GPU at most 7 ms (the "2x plus FSR at 120" tier holds p95),
3x at most 11 ms ("3x at 90").

| ID | Item | Expected (3x) | Effort | Status |
| --- | --- | --- | --- | --- |
| DR-2.0 | **Texture loads without the copy engine** (from DR-0.1): write the untiled texels into the image from a compute shader through a raw-bits storage view instead of `vkCmdCopyBufferToImage` from the scratch buffer, for uncompressed single-level 2D textures | Most of the 13 ms texture-load time at 3x; some at 1x | M | **Done** (see Progress) |
| DR-2.1 | **Mixed resolution**: the shadow passes (the 1280-wide 1x sun depth, the 1040-pitch D24S8) and the half and quarter post chain at 1x while the scene renders at the chosen scale; needs a scale per surface and resolves between scales (PB-1.10) | ~1.9 ms for the sun shadow (knock-out) | L | Open |
| DR-2.2 | **Lazy resolve aliasing** (Unleashed's model, PB-1.2/PB-8.7): a resolve destination sampled before its source is redrawn binds the executor surface or a persistent native texture; the mirror write stays for CPU-read resolves; present from the native surface (PB-1.4). Note the Android lesson: reloading resolve-sourced textures early broke FH1's HDR chain (ANDROID_60FPS A60-2), so aliasing must track write generations exactly | ~2.6 + 0.3-0.6 ms (knock-outs) | L (high risk: 20 resolve kinds, several formats per address, exact `24_8` bits) | Open, after DR-0.5 |
| DR-2.3 | **Transfers without nine passes on NVIDIA** (no `VK_EXT_shader_stencil_export`): copy the stencil byte from the words buffer into the stencil aspect with `vkCmdCopyBufferToImage` on single-sampled destinations; skip transfers a replay proves unread | ~1 ms of the at least 1.5 ms transfer floor | M | Open |
| DR-2.4 | **32bpp HDR main target trial**: host the 7e3 scene (`2_10_10_10_FLOAT`) as R11G11B10F plus a separate alpha strategy if any pass reads its 2 bits, instead of RGBA16F | Unknown; possibly the largest 3x lever if ROP or bandwidth bound | S-M to try | Open, after DR-0.1 |

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
| DR-5.5 | Translator: structured control flow for the 372 jumpy pixel shaders; the scale as a specialization constant (one pack for every scale) | Low on this GPU (PB-1.8a/b noise); packaging and live scale switches | M, S-M | Open |

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
| The list-executor identification is an inference | DR-0.4; DR-1 to DR-4 key on PM4 identity and do not depend on it |
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
| DR-2.0 compute texture copies | **Done (2026-10-02)** | Textures of at least 256x256 texels (scaled size), single-level 2D, in 4-, 8- or 16-byte uncompressed formats are created storage-capable (mutable format, extended usage) with an R32/RG32/RGBA32_UINT view, and `texture_copy_buffer_image.comp` writes the untiled words into them (`vulkan_texture_load_compute_copy`, on for desktop, off on Android where storage images may lose UBWC). GPU Trace of the same 3x frame: texture loads 13.1 to 3.9 ms, the frame 27.8 to 19.1 ms. Interleaved 3x race runs (`fh1-race-start-wait`, frames with at least 4,000 draws, 120 cap): frame median 24.7 and 25.2 ms against 39.9 and 41.0 ms, p95 39-42 against 62 ms, GPU median 25 against 39 ms. 1x unchanged (11.5-12.2 ms either way). Captures correct by eye at 1x and 3x. Two things seen on the way, not caused by it: the 3x race without it now measures 40 ms where PB recorded 22-24 ms on 2026-09-30 (a different route and frame selection, but worth a bisect), and the free-roam scene before the event shows posterized colour at 3x with or without it (and with 2_10_10_10 at 8 bits), while 1x is smooth |
| DR-0.1 Nsight GPU Trace | **Done (2026-10-02)** | Nsight Graphics 2026.3.1 GPU Trace of one 3x race frame (route frame 4380 of `fh1-race-start-wait`, single-sampled MSAA), launched through `launch-preview.ps1 -NsightCommand` (`run-fh1-render-test.py --nsight-gpu-trace`) with GPU performance counters allowed for all users. Passes labelled with `vulkan_debug_labels`. GPU frame 27.8 ms under trace. **The frame is neither ROP- nor bandwidth-bound:** over the frame the graphics engine is active 60 %, SMs 20 %, DRAM 17 %, ROP 3 %, depth ROP 2 %, and the front end stalls on wait-for-idle 35 % of the time. By label: **texture loads 13.1 ms (90 loads; the ten largest 0.4-0.8 ms each, graphics engine active only 15 % during them, the copy engine busy 40 % of the frame)**, draw renderings 5.6 ms (190), transfers and clears 4.7 ms (79), resolves 2.6 ms (96), uploads 0.1 ms, gaps 1.7 ms. The resolve-sourced reloads at 3x are scaled, frame-sized textures whose `vkCmdCopyBufferToImage` from the untile scratch buffer runs on the copy engine serialized with the frame. DR-2.0 is new and leads DR-2; DR-2.4 (32bpp HDR) has nothing to gain at 3 % ROP |
