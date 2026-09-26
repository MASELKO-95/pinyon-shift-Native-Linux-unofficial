# Car-body texture versions, 2026-09-26

The original eight-texture car-body pixel program
`E9CD565D9C61D037/0x16003F` reads fetches 0–6 and 13. Its shader-pack
manifest identifies fetches 5 and 6 as cube textures. The native material
snapshot path previously retained only fetch 1 and rejected cube resources.

The existing D3D12 source-frame snapshot path now accepts a single cube
resource and retains every used fetch for this exact car-body pair. It still
deduplicates by fetch words, allocation ID and payload generation within the
frame and respects the existing 64 MiB frame budget. The output renderer has
not switched to the original body program yet.

A RelWithDebInfo preview built and the installed-save
`continuous-default-start.fh1test` route exited normally with native race and
live UI enabled. One-time diagnostic logging showed successful snapshots for
all eight fetches: 0, 1, 2, 3, 4 and 13 as 2D views; 5 and 6 as cube views.
The respective snapshot allocations were 1,441,792; 1,441,792; 65,536;
65,536; 3,932,160; 2,162,688; 196,608; and 3,932,160 bytes when ordered
by fetch number. Native scene and HUD promotion continued through output
frame 5031. The diagnostic logging was removed after this check.

The remainder scene now carries the body program's one sampled bool/loop word
in `SNR03R6`; older fixture versions remain readable. The updated live race
route exited normally and promoted complete native frames through output
frame 5031, exercising the R6 writer and parser. The next body-program slice
must bind 2D/cube descriptor tables and samplers alongside the captured pixel
constants. Merely loading its bytecode without those bindings is incomplete.
These snapshots add capture cost; measure it after the original body program
is visibly useful.
