# Initial color producer inputs — 2026-09-26

The ordered selected-frame capture now exposes a bounded range of draws with
their owned index and vertex bytes, final constants, fetch state, viewport,
scissor and pinned texture identities. The native replay locates the
initial color copy at guest base `484626432`, finds the preceding offscreen
clear and second copy, and checks the intervening draw range. It contains
13 draws: one depth-only draw, eleven indexed triangle-strip color draws
that each read two textures, and one four-vertex feedback draw that reads
the first copied color version.

The replay also resolves each draw's original vertex and pixel bytecode
from the installed shader pack and borrows all pinned texture resources
through the native output callback. An unavailable input rejects the
selected replay before presentation. This establishes that the bounded
post-clear producer has replay inputs in process; it does not yet issue
those draws, reproduce the earlier depth-only work, or replace the guest
texture read. The next slice is an owned offscreen color/depth target and
original-shader draw execution at these ordinals, followed by the feedback
copy and later consumer binding.

The RelWithDebInfo short race route logged `draws=13 color=11 feedback=1`
for source frame 5001 after all shader and texture checks. The frame
promoted with its same-frame HUD and the route exited normally. The strict
ordered-frame verifier passed with 3,051 draws, 97 copies, 21 clears,
complete geometry/state and 415 pinned texture versions; the UI verifier
passed all 164 draws. Evidence is under
`.local/ray-ui-native-promotion-20260925/ordered-producer-resources-output/`.
