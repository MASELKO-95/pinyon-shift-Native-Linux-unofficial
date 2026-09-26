# Live native shadow replay — 2026-09-25

The existing native scene draw and original three-pair HUD replay now run
against a separate D3D12 render target for a bounded 24-source-frame race
window. The callback returns the untouched compatibility output for display.
The feed retains eight recent in-memory ordered UI frames and pins source
texture versions through the next output; it does not serialize each frame
or wait for GPU readback. Eight selected frames request diagnostic readback,
and completed submissions are drained later without a stage wait.

The saved-race route at `.local/ray-ui-shadow-render-c-20260925/` exited
normally. The source-frame logs around 6790–6807 had complete 170-draw
HUD batches on all but frame 6791. Native scene and UI draws both succeeded
on 15 output frames; the no-producer frame kept compatibility output.
Six consecutive shadow images, `native-shadow-6799.ppm` through
`native-shadow-6804.ppm`, have distinct SHA-256 hashes. Inspection of
frames 6802 and 6803 shows advancing race time and moving scene geometry,
with readable lap, place, leaderboard, minimap and speedometer. The
`native-ui-pilot-late.ppm` compatibility capture still has the original
world and HUD. The shadow world's road/terrain, car paint and scenery are
visibly approximate.

Both tracked follow-up routes also exited normally. The moving-race route
`config/render-tests/fh1-native-race-profile.fh1test` at
`.local/ray-ui-shadow-moving-20260925/` captured 24 source frames around
4980: 20 complete and four no-producer frames (4981, 4987, 4989, 4991).
The UI-admission stress route
`config/render-tests/fh1-native-ui-admission-stress.fh1test` at
`.local/ray-ui-shadow-stress-20260925/` captured 24 around 6790: 18
complete and six no-producer frames (every even frame 6792–6802).
The selected shadow images have six distinct hashes in the moving route and
five in the stress route. Adjacent saved frames update the HUD and scene;
the stress route's compatibility capture at output 6810 remained intact.
The bounded feed and readback queue stayed within their eight-frame caps.

The first offscreen run reached scene replay but lacked per-frame final UI
texture identities. Extending the existing final-draw observer to the
shadow window fixed that root cause; capture completeness now rejects
missing texture identities. Source texture pinning for the scene was also
enabled in this window. The successful run had no device removal or
unbounded capture stall. Its selected PPM readbacks are diagnostic only;
ordinary shadow frames retain no readback buffer.

This establishes a short live shadow segment, not visible native takeover.
The ordered world stream still uses the existing scene snapshot consumer,
and a no-producer UI frame still lacks an explicit retained target/version
dependency. Before RAY-03, prove that dependency and preflight both native
passes so a failed second pass never leaves a partially replaced guest
output. Mode transitions and the retained-state path remain to be run with
native takeover.
