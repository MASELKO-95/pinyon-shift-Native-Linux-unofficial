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
at texture descriptors 2–3 and fetch 0 at 5–6. After correcting those slots,
the saved native image still lost most visible spectators. Nearby frames also
failed the existing pinned-2D-view check for a manager texture, so a full
original-material takeover would intermittently fall back to compatibility.
That replay was removed; the visible flat manager material remains active.

Next, identify which fetch/version fails the 2D-view check and compare the
pixel program's descriptor and alpha/depth behavior with one matched Xenos
frame. Admit original-material draws only after they remain visible across a
short continuous route. Keep the current flat material for draws that cannot
use an exact texture version.
