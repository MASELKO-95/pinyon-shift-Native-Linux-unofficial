# Native mode boundary and compatibility control

The installed-save `fh1-native-race-mode-boundary.fh1test` route completed
normally on the current native build and on a compatibility-only control.
`verify-native-race-mode-boundary.py` passed the native run: race output was
native while pause, free roam and title used compatibility output. The
compatibility-only run correctly fails that verifier's native-race assertion,
so it is only the visual control.

Both `title-settled.ppm` captures show the same saturated, striped background
behind readable Single Player and Multiplayer menu text. The noise is thus
present without native takeover. This evidence clears the title artifact as
a native mode-handoff regression; it does not fix the underlying compatibility
rendering problem or prove every menu state.

Ignored captures:
`.local/ray-car-stencil-mode-20260927/` and
`.local/ray-compat-mode-control-20260927/`.
