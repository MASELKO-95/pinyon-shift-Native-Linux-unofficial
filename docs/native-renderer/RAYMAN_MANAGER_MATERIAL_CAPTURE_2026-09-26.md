# Live manager material capture

The live manager sidecar now retains each draw's pixel specialization, sparse
pixel constants, 40 bool/loop words, and the final identities of texture
fetches 0 and 13. It stays aligned by draw sequence with the existing manager
geometry fixture; the persisted `SNR03M1` format is unchanged. This supplies
the inputs needed to try the original manager pixel program without guessing
from geometry alone.

The installed-save race route
`.local/ray-ui-native-promotion-20260925/manager-rejection-short.fh1test`
exited normally using the RelWithDebInfo preview. At source frame 5000 it
captured 117 manager draws, 3,276 pixel words and 234 texture identities in
one run. The selected ordered stream still dispatched 1,654 supported draws.

An experimental original pixel replay used
`68150A8E959006CD/0x15001f`. The installed shader pack declares fetch 13
at texture descriptors 2–3 and fetch 0 at 5–6. The first trial still lost
most visible spectators because it uploaded a sparse 256-register buffer.
Shader disassembly shows a compact seven-float4 buffer and reads fetch 13
before fetch 0 in its two descriptor triplets. With packed constants and the
correct triplets, the saved native frame shows textured spectators. Nearby
frames contain an unpinned or non-2D manager texture; those individual draws
now retain the flat material instead of rejecting the whole native frame.
The short route exited normally and promoted output frames 5001–5006,
including frame 5004 where the earlier all-original trial fell back.

The SDK's normal native-material snapshot filter did not include the manager
shader pair. That explains why the selected trace frame was textured while
ordinary frames fell back to flat: trace mode pins every used texture. The
normal path now snapshots manager fetches 0 and 13. A RelWithDebInfo run of
`.local/ray-ui-native-promotion-20260925/continuous-race.fh1test` saved a
textured crowd at frame 5290. It promoted all 309 observed output frames
4993–5301 with no native draw rejection or snapshot-limit warning. The
fallback remains for resource versions outside the supported pinned 2D path.

Next, compare one matched Xenos frame and perform an unscripted drive before
calling the crowd quality gap closed.
