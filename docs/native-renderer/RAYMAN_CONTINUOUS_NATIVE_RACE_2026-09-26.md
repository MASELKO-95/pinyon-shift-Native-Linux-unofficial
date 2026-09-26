# Continuous native race presentation, 2026-09-26

The opt-in complete-frame scratch path now continues past its 24-frame pilot.
`--pinyon_shift_native_race=true --pinyon_shift_native_ui_live=true` captures
race UI draws and target events, combines their replay with the current owned
scene snapshot, and promotes only a completed 1280×720 R10G10B10A2 frame.
The existing guest output remains the whole-frame fallback. The UI start frame
and scene capture start frame remain optional diagnostics.

## Checks

All runs used the locally built RelWithDebInfo preview, the installed AppData
preview save, and render-test scripts. Each exited normally.

| Run | Result |
| --- | --- |
| `continuous-race.fh1test`, native start 4980, stop 5300 | Native promotion continued for more than 300 moving race frames. Captures at 5040, 5100, 5200 and 5290 changed as the world advanced. |
| `continuous-toggle.fh1test` | Native output through frame 5040, complete guest output during the off interval, and native output again from frame 5053. Captures around both boundaries retained the HUD. |
| `continuous-pause-resume.fh1test` | Native before pause, complete guest pause menu at 5075, and native race output again at 5110 and 5130. |
| `fh1-native-race-mode-boundary.fh1test` | Native race output, compatibility pause menu, and compatibility free roam after race retirement. |
| `continuous-default-start.fh1test` | With only the two opt-in flags above, captures at 5010 and 5020 showed advancing native world and readable HUD. |

The inspected late frame at 5290 had no visible HUD. A temporary draw census
found some low-draw UI frames. Requiring a minimum draw count forced whole-frame
compatibility on one such frame, but the guest output also lacked the HUD
there. The minimum was removed: it misclassified a guest-authored UI state and
caused unnecessary world alternation. The retained-target check still guards
no-producer frames with intervening target writes.

This is a presentation milestone, not complete RAY-00/01 replay. The scene is
still built from the existing owned snapshot rather than the full authoritative
world event stream. The road is flat, the car is opaque, and visual fidelity is
below a playable target. Longer unscripted driving and measured frame time
remain open. The next work should replace the largest visible world/car gaps
using ordered capture and original shader/resource inputs, then run the longer
drive before optimizing duplicate rendering.
