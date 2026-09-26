# Indexed input and large-scene trial — 2026-09-26

The eleven indexed initial-color draws were uploading the captured guest
index bytes unchanged. Their 16-bit indices use guest byte order: an example
starts `00 00 00 01 00 02`, which D3D12 would read as `0, 256, 512`
instead of `0, 1, 2`. The offscreen replay now requires the observed 16-bit
index shape and converts each index before upload. A RelWithDebInfo run with
this change promoted source frame 5001 and exited normally. A temporary
diagnostic blit of its second native color version changed the geometry but
still showed large green/yellow regions instead of the scene. The temporary
blit was removed; visible consumers remain bound to pinned guest versions.
Artifacts: `.local/ray-ui-native-promotion-20260925/ordered-index-fixed-preview-repeat-output/`.

The next main-color candidates were two original indexed triangle-list
families, one with 8,700 indices and two textures and another with 9,300
indices and three textures per tile. The captured 32-bit index payloads
decode to bounded vertex ranges when converted from guest byte order. A
combined nine-scene-draw trial reached source frame 5001, then D3D12
reported device removal (`0x887A0005`, reason `0x887A0006`) and the route
crashed. The large-draw trial was removed from the working implementation.
Its cause is not isolated; it must not be admitted merely because its CPU
capture checks pass. Artifacts:
`.local/ray-ui-native-promotion-20260925/ordered-large-scene-output/`.

Attempts to isolate the two-texture family on source frames 5001, 5000,
5002 and 5003 exited normally but did not execute selected-frame ordered
replay. Their ordered captures had zero UI draws and retained a prior UI
source, so the same-frame UI-suffix admission gate correctly declined them.
This cadence makes fixed-frame shader trials expensive and is the next
capture/replay issue to solve. Capture a complete UI-producer frame or
explicitly replay the retained UI target version before testing another
large GPU draw; then isolate the device-removal cause one family at a time.
