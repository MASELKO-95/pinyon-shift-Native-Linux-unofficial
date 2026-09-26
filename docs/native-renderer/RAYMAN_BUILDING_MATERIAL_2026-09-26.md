# Original facade material, 2026-09-26

The five-texture `0CBC533419F61E0D` / `56D45C45966FD938` track
program now uses the original pixel shader, packed constants and fetch order
`5, 7, 13, 4, 0`. The backend pins its source-frame texture versions. Original
pixel programs now have sampler tables large enough for the translated
bindless indices; the prior eight-descriptor table was too short for this
program and the car body/glass programs.

The first live attempt still fell back at `track_structure_texture`. A
diagnostic run found the 128-material snapshot limit at source frame 4102,
with 44,892,160 of 64 MiB used. Raising the bounds to 256 materials and
96 MiB admitted that early race frame. The limit warning remains so a future
scene that exceeds the new bound is identifiable.

The RelWithDebInfo build passed. The saved AppData race route
`.local/ray-ui-native-promotion-20260925/manager-rejection-short.fh1test`
exited normally with native race and UI enabled. Its log reported 836
promoted output frames, including source frames 4102 and 5000, and no
material-snapshot limit. The 67 compatibility frames around source frame
4266 were rejected at `prepare_remainder`; native presentation recovered
later in the same run. The saved
`.local/ray-ui-native-promotion-20260925/building-promoted-full-output/race-moving.png`
shows a readable road, car and HUD without the neon facade seen in the
earlier trial. Game position varies across scripted runs, so this is not a
matched image or performance comparison.

The facade is usable in this route, but structures, crowd colors, terrain
and car lighting remain approximate. The full ordered clear/resolve/target
stream and an unscripted drive are still open.
