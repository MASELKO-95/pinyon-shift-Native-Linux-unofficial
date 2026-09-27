# Current live race CPU checkpoint

The current RelWithDebInfo build ran the installed-save
`fh1-native-race-profile.fh1test` route to normal exit with native race and
live UI enabled. The trace-enabled performance CSV at
`.local/ray-post-stencil-profile-20260927-v2.perf.csv` has 28 rows for
source frames 5002–5029. Medians were 93.15 ms frame time, 42.22 ms guest
draw-issue CPU, 11.04 ms prepared snapshots, 8.12 ms prepared observer, and
34.21 ms native output CPU. The trace flag adds logging overhead, so this
is a ranking checkpoint rather than a clean before/after benchmark.

Three sampled native scene callbacks (frames 5010, 5020 and 5030) spent
18.3–21.9 ms in scene work and 2.9–3.8 ms in UI. Within scene work, parsing
took 5.2–7.0 ms, draw preparation 5.1–6.2 ms, and frame-resource creation
4.5–5.6 ms. These samples show no single small helper that can remove the
roughly 70 ms gap to compatibility. Keep the next performance change tied to
rolling ordered ownership and a proven removable guest pass, with paired
promoted-frame timing and fallback checks.

An initial capture invocation failed before the game loaded because its
render-test output directory already existed. The rerun used a fresh output
directory and exited normally; the failed invocation contributes no timing
data.
