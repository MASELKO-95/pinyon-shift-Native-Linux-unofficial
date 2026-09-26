# Ordered depth version snapshots — 2026-09-26

The selected-frame native path now retains a separate depth resource at
each of the three main-scene depth-resolve ordinals. It validates the
captured source selection, EDRAM base, rectangle, tile height and consecutive
guest destination addresses. Each depth copy must precede its paired color
tile copy. The native depth surface is snapshotted into three owned D32
resources; they remain alive until the GPU submission completes.

The full-resource snapshot is deliberate. D3D12 requires whole-subresource
copies for depth/stencil resources, so the partial `CopyTextureRegion` used
for color tiles is invalid for depth. The rule is in
[Microsoft's CopyTextureRegion documentation](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copytextureregion).
Each snapshot therefore contains the entire native depth surface as it
stood at that ordinal. It preserves three native versions but does not yet
reproduce the guest's prior depth pixels outside the updated tile.

The RelWithDebInfo continuous race run logged depth/color pairs at source
frame 5001 with cumulative heights 256, 512 and 720. The selected frame
promoted with the same-frame HUD, its saved native image remained readable,
and the route exited normally. The strict ordered verifier passed with
3,218 owned draws, 94 copies, 20 clears, 420 pinned texture versions and
88 complete nonzero-size copy inputs. The UI verifier passed all 166 draws.
Evidence is in
`.local/ray-ui-native-promotion-20260925/ordered-depth-versions-output/`.

This is an owned depth-version pilot, not completed depth replay. The
initial 1280×720 depth texture is still produced by compatibility work;
the three native snapshots are not yet shader-readable or bound to their
later consumers. The initial color texture at guest base `484626432` is
sampled much more widely and remains the next producer to reconstruct.
