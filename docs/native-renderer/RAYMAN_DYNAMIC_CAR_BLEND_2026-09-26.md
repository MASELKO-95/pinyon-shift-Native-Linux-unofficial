# Dynamic car blend weight, 2026-09-26

The original no-texture car pixel pair
`CC2F3F4B3FBA53F5` / `CDA93D7ADC1991D8` was admitted only when
register 47's blend weight was exactly zero. A repeated native race route
rejected about 70 consecutive frames at `prepare_remainder` while that
captured weight rose to about 0.01. The other two required registers, packed
constant count and system output bias were valid. The original pixel program
already receives register 47, so the zero-only restriction was removed; the
remaining layout checks stay in place.

The RelWithDebInfo build passed. The installed-save short race route
`.local/ray-ui-native-promotion-20260925/remainder-guard-removed.fh1test`
exited normally. It promoted source frame 4279 and the surrounding interval;
the saved `remainder-guard-removed-output/dynamic-car.png` shows a moving car,
road and readable HUD. There was one later native rejection at
`track_structure_texture` near source frame 4380. Scripted race position
varies between runs, so this verifies admission and gross visual continuity,
not a matched pixel comparison.
