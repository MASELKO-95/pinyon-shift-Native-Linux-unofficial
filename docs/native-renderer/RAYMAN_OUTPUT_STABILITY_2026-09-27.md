# Long output-frame race stability, not a driving pass

The older `fh1-race-sustained.fh1test` uses `clock-hz 60`. On the current
native build it completed script frame 6920 while the renderer had only
reached output frame 5279, so it exercised about 280 native output frames.
That route cannot stand in for a long native presentation check.

The new `config/render-tests/fh1-native-race-output-stability.fh1test` paces
by output frames. The installed-save run exited normally at output frame
6920. Captures at 5200, 5600, 6000, 6360 and 6800 retained the native
scene and readable race HUD. The runtime log recorded 1,920 frames with
`scene=true ui=true promoted=true` from 5001 through 6921. Frame 6538 is
absent from those promotion lines; the log alone cannot classify that one
frame as a fallback or an unobserved output. There were no native draw/output
rejection warnings in this run.

This route still does **not** qualify playability. Full throttle without
steering puts the car against a barrier: its position changes by less than
two metres from output 5200 to 6800, and the late captures show zero speed.
Two bounded steering trials (a short partial-left input and a longer full-left
input) did not produce a sustained drive, so no further fixed-input tuning is
planned. An interactive race with steering remains required for control
response, driving defects and fallback observation.

Ignored evidence: `.local/ray-long-race-post-stencil-20260927/` for the
simulated-clock route, `.local/ray-long-output-post-stencil-20260927/` for
the output-paced route, and `.local/ray-steering-*-trial-20260927/` for the
two short steering checks.
