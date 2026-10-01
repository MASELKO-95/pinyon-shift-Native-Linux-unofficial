# Experimental reflection mipmap replacement — 2026-09-10

Historical: describes the Xenos-era renderer's reflection mip replacement, removed in `bccf126` and `f6492b7`; kept for reference.

Status: **focused mipmap implementation complete; enabled by default at symmetric
1x/2x/3x with compatibility fallback.** The remaining B epic is deferred and open.
This is a bounded renderer improvement, not complete Xenos retirement or a
new hardware-requirement claim. Subsequent user testing reports green reflection
flashes and severe Carson slowdowns; see the open follow-up below. The bounded
capture checks do not establish visual correctness across normal gameplay.

## Runtime behavior

`fh1_native_reflection_mips` defaults to `true`. Use
`--fh1_native_reflection_mips=false` at launch to restore the original mip draws
and copies; the setting requires restarting the game.

For an admitted six-face reflection cube, eight native compute dispatches per
face replace **48 legacy render draws and 48 resolve copies**. The filter uses
existing shared memory at 1x and existing scaled resolve storage at 2x and 3x. It adds
no persistent texture mirror. Root signatures and compute pipelines are reused.

The renderer checks a fresh private command snapshot, an address-independent
signature of all non-address command bits, eight source/destination layouts,
current external shader/constants/vertices, physical bounds and overlap. It
reuses `SharedMemory::CopyCpuSnapshot`, including its ownership and concurrent
write checks. Addresses are not cache identities; relocation is supported.
Inherited scissor, index and constant-base state, query activity, predication,
render-target path and supported scales gate admission. Every scaled source
page must be authoritative; the page check takes one lock. Unsupported or
changed inputs retain the original execution path.

Original state restoration, events, scratch clears and ownership transfers
still execute. **Six cached guest lists and all 2,352 packets per cube remain.**
Removing their producer/decoding is deferred B3 work. Reflection face rendering,
cube import, other render targets and the general compatibility renderer remain.

The integer recursive box filter deliberately allows small rounding differences
from the original float path. Prior captured comparisons observed maximum RGB
errors of 2/1,023 at 1x and 3/1,023 at 2x; these are captured bounds, not a universal
accuracy guarantee. Update frequency, cube resolution and animation timing are
unchanged by this implementation.

## Validation

- Standalone contract assertions pass all six faces, full address relocation,
  external-input mutation and unavailable/GPU-owned input rejection, malformed
  commands, truncation and inherited-state rejection.
- The Release application/renderer build passes. FXC `cs_5_1 /O3 /WX` reproduces
  both shipped shader byte arrays exactly. Generated-header whitespace cleanup
  leaves those arrays unchanged.
- Both guarded diagnostic scales and both final moving captures pass native
  publication, all 54 imported cube subresources and an actual later consumer.
  Final captures have 48 native dispatches and zero original mip draws. The
  CPU reference matches **2,211,840 bytes at 1x** and **8,847,360 bytes at 2x**,
  with no writes outside active mips. Moving-frame contents differ from prior
  contents, so these checks do not merely accept a stale cube.
- The 1x clear-history check now passes all 48 clears and both real ownership
  transfers: 20,480 incoming/20,480 outgoing fragments in the control, and
  4,096 incoming/20,480 outgoing fragments in the native capture. The 2,048
  differing padding pixels originated in different incoming data and survive
  to the later consumer. Earlier equivalent 2x checks cover 8,192 differing
  pixels. No clear or transfer is removed by the retained code.
- Final default-enabled 1x/2x moving smoke runs exit normally, deliver all
  14 inputs, pass four capture-clock checks and show forward motion, braking
  and reverse motion with intact car, road, crowd and HUD. They are bounded
  smoke checks, not matched moving-performance or general streaming tests.

| Final moving capture | Checked frame | Imported pixels | Later consumer event / cube |
| --- | ---: | ---: | --- |
| `20260910T194311Z-p29932`, 1x | 1898 | 524,286 | 8438 / 8078 |
| `20260910T194801Z-p18232`, 2x | 1854 | 2,097,144 | 8149 / 8064 |

Both consumers bind the six-face, nine-level R10G10B10A2 cube through
VS `C34795A841E7DEFF` / PS `21B70A5E4C9CFD11`. Adjacent frames were captured
but are not counted as additional inspected GPU proofs.

## Clean performance result

Eight prospective off/on/on/off runs use the same clean EXE, renderer, runtime,
complete scale-specific packs, catalogs, input route and OS sampling. Corpus,
discovery, producer tracing and per-face logging are off. No competing build or
replay runs during gameplay. The stationary Recaro window is 20–40 seconds,
excluding the capture at 30 seconds ±250 ms. Images confirm matching location,
car/HUD state and zero speed; crowd/traffic phases vary.

Values are means of two per-run statistics per mode, original → native.
GPU values are asynchronous frame samples; private memory is the mean of process
peaks, not the memory cost of this pass alone.

| Scale | Median ms | p95 ms | p99 ms | GPU ms | Peak private MiB |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1x | 15.248 → 15.576 | 17.698 → 17.766 | 20.203 → 20.166 | 14.448 → 14.995 | 2560.6 → 2549.7 |
| 2x | 16.760 → 16.534 | 19.721 → 19.419 | 22.135 → 22.416 | 16.688 → 16.408 | 4804.2 → 4794.2 |

The **1x whole-frame result is inconclusive**: median +2.15%, p95 +0.38%, p99
−0.18%; enabled medians fall within the two control medians. At 2x, median
improves 1.35%, p95 improves 1.53%, and p99 rises 1.27%. These short, variable-scene
results justify no large FPS claim or lower published hardware requirement.
Whole-process CPU changes by +0.29% / −1.24% at 1x/2x. Mean GPU dedicated-memory
peaks change from 1,139.0 to 1,129.8 MiB and 3,348.6 to 3,343.3 MiB; there is
no meaningful memory reduction to advertise.

The earlier instrumented 2x six-list comparison measured GPU cost
**1.075200 → 0.100352 ms** and CPU cost **0.4011 → 0.1980 ms**. That establishes
why the targeted work is useful; it is not the clean whole-frame saving above.
Retention here is an **experimental implementation/work-removal milestone**.
Broader performance, sustained streaming, full races and lower-end hardware
qualification remain open. Disable the setting if broader testing finds a
regression; the original path remains available.

| Scale / order | Session | Last reported native faces | Avoided draws and copies, each |
| --- | --- | ---: | ---: |
| 1x / a1 | `20260910T192511Z-p30420` | 0 | 0 |
| 1x / b1 | `20260910T192730Z-p26516` | 10,680 | 85,440 |
| 1x / b2 | `20260910T192902Z-p29004` | 10,638 | 85,104 |
| 1x / a2 | `20260910T193039Z-p23012` | 0 | 0 |
| 2x / a1 | `20260910T193252Z-p32892` | 0 | 0 |
| 2x / b1 | `20260910T193430Z-p15512` | 10,752 | 86,016 |
| 2x / b2 | `20260910T193613Z-p6704` | 10,740 | 85,920 |
| 2x / a2 | `20260910T193759Z-p9612` | 0 | 0 |

Admission totals are periodic lower bounds, not exact shutdown totals. All
reported enabled candidates admit without fallback in these eight runs. Every
run records one existing invalid simulation delta during startup, and zero in
the compared world interval. The original whole-session verifier failure is
preserved; this change does not fix or close NPC/UI timing.

## Source, binaries and reproduction

SDK source pin: `d65d59d61c3b2408b1096564c8fb440b8138c02b`. Main source before this checkpoint was `cfcaf792`.
Only mipmap changes are retained from this work; the other B candidates keep
their prior defaults, and A6 remains symmetric-1x-only.

| Qualified local artifact | SHA256 |
| --- | --- |
| EXE | `EC2E5F097A3D513AE945B42B9E1EE01822A9E2DA7EEA08F5DD91B7E8F3243D30` |
| Renderer DLL | `58864846D58F8C9D173EB87D6228D76E8FBC47FCD9B06A1CC4FEF9B37F72899A` |
| Runtime DLL | `6B97FB8B1CF15DBBB1C6A0AD762399F40F3AAFAC1E4D0CB8B818D82DFB8E24CC` |

Both complete offline packs contain 22,012 entries. Their SHA256 values are
1x `1636179BF8633D7406C7C3C735DD600C0C05666D8A8A8CAC188433730A38C026` and
2x `D6E62162510BE0EDFC0CA4D1624B498F51024F7BC5AC2597A23C37929E030A3E`. The complete 1x pack is restored
after testing. Captured game commands, memory, screenshots and RDCs remain local.

The assertion tool `tools/check-fh1-mip-contract.cpp` and the command-stream
contract it exercised were removed with the Xenos renderer; the FH1 native
executor does not replace the reflection mip chain.

The local fixture supplies `face-0.bin` through `face-5.bin` and the external
snapshots in `memory/<hex-address>.bin`; captured game data is not distributed.
Local evidence and reusable helpers are under `.local/native-renderer/b2/`:
`reflection-mip-runtime-v2/`, `reflection-mip-retention-v2/comparison.json`,
`reflection-mip-runtime-motion-1x-v2/`, `reflection-mip-runtime-motion-2x-v1/`,
`reflection-mip-clear-history-1x-v1/` and `reflection-mip-scratch-transfers-1x-v1/`.
`run-mip-retention-v2.ps1`, `check-mip-retention-run.py`,
`summarize-mip-retention.py`, `run-mip-proof-replay.py` and
`check-mip-scratch-transfers.py` reproduce/check their respective evidence.

Preserve the earlier combined prototype's zero-admission 2x run. Later rejection
instrumentation did not reproduce it, so no original root cause is claimed.
The retained implementation removes fixed-address/captured-list admission and
passes new 1x/2x checks. Also preserve the first missing-route capture, the
shutdown-summary comparison failure, the capture-name filter failure and the
strict-mode optional-property failure. The latter capture already had complete
RDCs and a normal game exit; its explicit post-check and replay pass.

## Deferred work

The broader B1 scene/family inventory, B2 geometry/texture migrations and sustained
streaming/tails, B3 upstream packet removal (including the mip lists), B4 quality
profiles/NPC/UI timing and the stopped HUD/recycling comparisons remain open.
C's full Xenos retirement and actual lower-hardware qualification also remain open.
That work now continues in the [Xenos retirement backlog](../XENOS_RETIREMENT_BACKLOG.md);
do not automatically restart the other experiments.

## User follow-up: Carson and green reflection flashes

Reported after the latest-build launch on 2026-09-10:

- [ ] Intermittent green flashes on the main car's rear glass (reported as
  "back shield"), suspected by the user to involve reflections.
- [ ] Carson town runs at roughly half the usual performance.
- [ ] A Carson race has severe performance problems; event name pending.

Session `20260910T200636Z-p26860` ran from 20:06:36 to 20:12:12 UTC and
terminated normally. The last periodic counters show mipmaps enabled, 87,898
candidates, 87,894 native faces and four fallback lists. These are aggregate
lower bounds, with no location or flash-event association. They establish that
the new path ran, not that it caused any of the three symptoms. No matching
mipmap-disabled Carson comparison or flashing-frame GPU capture exists yet.

The three current on-disk binary hashes match the qualified artifacts above.
The process-start log contains older embedded build metadata; do not use that
metadata as the source revision or current on-disk binary identity.
Logs, session JSONL, performance CSV and a SHA256 manifest are preserved locally
at `.local/native-renderer/b2/carson-user-report-20260910/`. The CSV spans menus,
free driving and racing without scene markers, so a whole-session statistic
would not establish Carson-specific performance.

Follow-up investigation: Hot Hatch Hustle reproduces the race slowdown with
native mipmaps disabled. The [geometry cache fix](CARSON_GEOMETRY_CACHE_FIX.md)
removes repeated eviction of recently used owners and materially improves the
short race comparison. Mipmap-enabled 1x/2x race runs also pass smoke checks.
Longer Carson town/race acceptance remains open. Green flashes have not been
reproduced; a clean-frame native mip publication check cannot close that issue.
Capture the affected car and failing reflection frame next. Other B work remains
deferred. The hashes above describe the original mipmap milestone; the linked
follow-up records the newer tested renderer identity.

## 3x extension — 2026-09-12

The replacement now covers symmetric 3x, which previously fell back to the
legacy mip draws and resolves. Two defects were removed:

- `GenerateFh1ReflectionMips` rejected scale 3 and had no 3x ScaledOffset. The
  added `fh1_reflection_mip_3x` compute shader filters the 3x3 subpixel block of
  each source texel using the group-based scaled resolve layout (guest groups of
  four guest texels of one guest row, `scale_x * scale_y` sixteen-byte host
  groups stored column-major). The 2x specialization of that addressing is
  byte-identical to the shipped, GPU-validated 2x shader.
- The native path's scaled base offset was `uint64_t(base_address) * 4`, the 2x
  area scale, so at 3x the compute shader ran 4/9 of the way into the scaled
  range and hung the device (`0x887A0006`) during the offline producer. It now
  uses the current area scale. 1x and 2x are unchanged.

Route-J (outpost to open road) before/after at 3x, pose-matched: the car-region
clipped-pixel fraction falls from 1.97 % mean / 4.28 % max with 46 of 78 pairs
above 1 % to 0.013 % mean / 0.02 % max with none, the blown magenta glass and
roof and its halo are gone in native-resolution crops, and 30 036 of 30 037
candidate faces are published natively (`native_faces=0` before). 1x comparison
of the same route shows no change. The 3x shader is checked byte-exactly against
a CPU reference on the GPU (19 906 560-byte chain, zero differences, live poison
control) before any game run. Why the legacy mip chain is wrong at 3x — the
suspects are the sub-tile mip render targets and their scaled resolves — is not
established; the fix bypasses that path instead of repairing it.
