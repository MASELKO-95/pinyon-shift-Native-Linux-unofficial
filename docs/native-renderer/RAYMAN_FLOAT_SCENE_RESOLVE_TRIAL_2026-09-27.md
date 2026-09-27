# Float scene resolve trial, 2026-09-27

The guest's main scene writes a four-sample float target and resolves it to
10-bit UNORM with exponent bias -2. The current native world writes directly
to a single-sample 10-bit target. A bounded shadow trial tested whether
retaining float values from the current native shaders, then multiplying by
one quarter at each ordered color-tile copy, improved the final image.

The temporary code changed native world and scene-producer pipeline targets
to single-sample `R16G16B16A16_FLOAT` and drew each tile into the existing
10-bit destination with a quarter-scale conversion. It left visible output
on compatibility, preserved whole-frame fallback, and did not claim to
implement the guest's four-sample resolve or full EDRAM alias. The installed-
save race-profile route exited normally. Outputs 5004–5023 logged original
producer replay with scene and HUD available; eight adjacent native shadows
were saved under `.local/ray-float-scene-shadow-20260927/`.

The saved `native-shadow-5016.ppm` is markedly darker than the earlier
ordered shadow at the same output number: road, scenery, car and speedometer
lose detail, while the sky remains flat. The runs are not synchronized
game-state pairs, so this is a rejection of this approximation, not a
quantitative parity result. The temporary code was removed and the preview
rebuilt from the restored source.

The captured guest quarter-scale rule cannot be applied directly to the
current native shader mix. A later resolve change must first prove the
actual native float producer values and sample/alias behavior for one tile,
then compare that tile and the final frame against same-run guest output.
Do not reintroduce a display brightness scale or broaden this trial to all
frames without that source contract.
