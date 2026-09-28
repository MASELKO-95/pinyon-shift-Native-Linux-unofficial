- [x] On native render, the main screen is completely green (fixed 041d541)
  - Not native: byte-identical with native off. Caused by
    `--pinyon_shift_skip_opening_movies=true`: the skip hook
    (`PinyonShiftCompleteOpeningMovie`, 0x82E5D8AC) ends *every* XMedia movie,
    including the title loop `media/ui/videos/PressStart.wmv`, so its YUV
    planes stay zero (RGB 0,77,0). With movies playing the title is correct.
    Fix idea: only skip `media/ui/videos/splash_intros/*.wmv`.
- [x] Native render not working, defaulting to Xenos (obsolete)
  - The Xenos renderer and the renderer choice were removed (`e9c7ba7`,
    `b650ce7`); the native executor is the only renderer. The old opt-in
    flags and the SNR03 vegetation fallback no longer exist. The route
    matrix (`fh1-fmv`, `fh1-opening-sync`, `fh1-rewind-sync`,
    `fh1-race-sync`, `fh1-modes-sync`, `fh1-buy-car`) passes natively with
    zero pack misses and zero executor skips.
- [x] (fixed 041d541) When going back to title screen on native render, the "Single Player" select screen is completely corrupted in texture super pink noisy
  - Same root cause as the green title (movie skip): stale data in the
    PressStart.wmv planes. Identical in compat-only runs; clean with movies on.
- [ ] Car selection on an event has either pink correupted textures or mangled car textures/models
  - Not native and not the movie skip: identical noise in compat-only runs
    and with movies on, unchanged in every capture since 2026-09-21. The car
    card images (top-right card, card next to the $ tile) decode as striped
    pink noise; likely a texture format/tiling decode issue in the texture
    cache (tracked as NP-0.6 in docs/NATIVE_PORT_BACKLOG.md).
