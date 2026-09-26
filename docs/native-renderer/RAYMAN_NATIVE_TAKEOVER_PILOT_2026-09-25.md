# Bounded native race takeover pilot — 2026-09-25

The live scene and ordered textured HUD replay now draw into a separate
1280×720 R10G10B10A2 target. Only after both passes succeed does the final
output hook copy that complete target into guest output. Failure keeps the
whole compatibility image; no partially drawn native image reaches it.
The existing `pinyon_shift_native_race` hot toggle controls promotion.

The UI-admission stress route with native enabled and source window
6790–6813 exited normally. All 24 observed output frames reported
`scene=true ui=true promoted=true`. The captured `scene-6804.ppm` shows
moving native world, car and readable original lap/place/time/leaderboard/
speedometer HUD. The world has large flat-color surfaces and a simplified
car, so visual quality remains below the intended playable state.

A temporary copy of the tracked hot-toggle route added captures for frames
5000–5022 and used source window 4998–5021. It exited normally. Native
output was visible at 5002 and 5004–5010; source frame 5002 had no UI
producer, so output 5003 kept the complete compatibility world and HUD.
The `native-race 5010 false` command restored compatibility at output 5011;
`native-race 5014 true` restored native at output 5015. Captured images
confirm those boundaries and advancing lap time. An initial temporary
script failed validation (`script_capture_order`) because its added capture
commands were out of frame order; the corrected script is the run above.

The tracked mode-boundary route also exited normally with the bounded native
window enabled. Its pause and post-retire free-roam captures show complete
compatibility images. The title capture has severe colored noise, but the
same artifact is present in the earlier
`.local/native-mode-output-boundary-final/` and
`.local/native-mode-capture-only-control/` captures; it is not established
as a regression from this promotion path. Title presentation remains an
open issue for the broader native-race mode boundary.

Evidence: `.local/ray-ui-native-promotion-20260925/` contains route copies,
PPM captures and selected PNG conversions. This proves bounded whole-frame
promotion and fallback, not RAY-03 completion. The feed is limited to 24
source frames, no-producer frames alternate to compatibility, and mode exit/
pause plus unsupported resolution still need fuller tests. RAY-00/01 still lack
an authoritative ordered world stream and retained UI target dependency.
