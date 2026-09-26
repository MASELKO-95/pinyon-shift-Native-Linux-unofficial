# Ordered world draw replay, 2026-09-26

Host commit `d8aec90` interleaves the six supported live world families by
their backend prepared-draw sequence. Track, procedural item, vegetation,
character, character-manager and remainder draws each bind their own pipeline,
index/vertex buffers, constants, viewport and scissor when dispatched. Missing
or duplicate sequence numbers reject native presentation for that frame.

The RelWithDebInfo preview build passed. The saved race route
`.local/ray-ui-native-promotion-20260925/manager-rejection-short.fh1test`
ran with `--pinyon_shift_native_race=true`,
`--pinyon_shift_native_ui_live=true` and info logging, using the installed
AppData preview state. It exited normally. The log admitted source frame 5000
with 1,162 core draws and character, manager and remainder families present;
output frame 5005 reported `promoted=true`. The saved
`.local/ray-ui-native-promotion-20260925/ordered-six-family-output/race-moving.ppm`
shows a native road, car, readable race HUD and spectator silhouettes.

This check establishes continued native promotion after interleaving and a
readable moving frame. It is not a matched pixel comparison: game time and
position varied between repeated scripted runs. Clears/resolves, unsupported
draws, and the HUD pass still need the authoritative ordered frame stream.
