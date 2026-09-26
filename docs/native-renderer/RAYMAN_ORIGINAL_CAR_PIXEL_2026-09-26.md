# First original car pixel program, 2026-09-26

The native remainder path now loads the packed translation of the no-texture
car pixel shader `CDA93D7ADC1991D8` at specialization `0x10001` for its
verified `CC2F3F4B3FBA53F5` vertex pair. The existing exact material check
limits this path to that pair. Its three captured pixel constant vectors and
system constants are bound to the original program; the handwritten
approximation for this pair is removed. A missing program or unexpected
constant layout rejects the complete native frame.

The RelWithDebInfo preview built successfully. The installed-save race route
`continuous-default-start.fh1test` ran to normal exit with only
`--pinyon_shift_native_race=true --pinyon_shift_native_ui_live=true` and
captured native world and readable HUD at frames 5010 and 5020. Visual review
shows no material change to the large red car body, because that body uses a
different eight-texture pixel program. This change proves one original car
program can share the live native output path; it does not close RAY-04 car
quality. The eight-texture body still needs its resource versions, samplers
and constants before the original program can replace the placeholder.
The tracked `fh1-native-race-profile.fh1test` route independently exited
normally, promoted native output through frame 5031, and logged successful
pipeline creation for this exact pixel hash and specialization.
