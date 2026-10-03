# Pinyon Shift on Android

**Status: alpha, for developers.** The game builds for Android, installs,
boots and reaches the title screen through the Vulkan renderer. It completes
the opening drive on the renderer-free null backend on an arm64 device. It
has been run on an Android 16 arm64 emulator on an Apple M4, not yet on a
phone or a handheld. The target is a high-end Android handheld or phone with
a Snapdragon 8 Gen 2 or newer (Adreno 740+), 12 GB of memory and Android 13
or later. The plan and its progress are in the
[Android port backlog](ANDROID_PORT_BACKLOG.md).

## What stays private

The package holds the game translated from your own disc, exactly as
`pinyon_shift.exe` does on Windows, so it is built on your PC and installed
on your own device, and it is never published or shared. The game files go
from your PC to your device over USB. Nothing is uploaded. The repository
refuses Android packages, libraries and signing keys (see
[legal](LEGAL.md)).

## Requirements

- A Windows PC where the game is already built (the launcher, or
  `tools/build-preview.ps1`): the Android build reuses the code translated
  there.
- The Android SDK command-line tools (Android Studio installs them) and JDK
  17. `pinyon.py android doctor --install` adds the pinned NDK, build tools
  and platform from `config/android-toolchain.json`, and shows the Android
  SDK license for you to accept (`--accept-licenses` answers yes).
- The device: arm64, Android 13 (API 33) or later, a Vulkan 1.3 driver,
  about 8 GB free, USB debugging turned on.

## From the PC to the device

In the launcher, once the game is built, **Build Android APK** does the
first two steps below. It asks you to accept the Android SDK license, and on
a PC with no Android SDK or JDK it fetches the pinned command-line tools and
Eclipse Temurin JDK 17 into the install folder (`.local/toolchain`), checked
against their SHA-256. A failure is described in
`.local/logs/android-error.json`. Then install the package and copy the
game with the last two commands.

From a terminal:

```bash
python tools/pinyon.py android doctor --install
```

```bash
python tools/pinyon.py android build
```

```bash
python tools/pinyon.py android install
```

```bash
python tools/pinyon.py android push-data
```

`build` cross-compiles the game (20 to 60 minutes the first time) and
packages `.local/android/pinyon-shift.apk`, signed with a key made on your
PC. `push-data` copies the extracted game (7.2 GB, 2,400 files) into the
app's folder on the device, `Android/data/com.pinyonshift.fh1/files/game/base`;
it resumes where it stopped if interrupted. Saves, settings, logs and mods
live beside it in `files/state`, with the same layout as on the PC, so a save
copies between the two as a folder.

Start the game from the launcher icon, or:

```bash
python tools/pinyon.py android run
```

`run --null-gpu` starts it without a renderer, and `run --route FILE` runs a
render-test route (`config/render-tests/`). `pull-logs` copies the logs,
crash reports and route output to `.local/android/device-logs/`.

## Controls

A controller (built in, Bluetooth or USB) works as on the PC. On a touch
screen, on-screen controls appear at the first touch: a steering stick
wherever the left thumb lands, throttle (RT) and brake (LT) under the right
thumb, A, B, X, Y, the bumpers, Back and Start. They hide 20 seconds after the
last touch, so a controller player never sees them
(`pinyon_shift_touch_controls` turns them off).

Android's Back button or gesture opens SETTINGS, and inside the menus it steps
back; tap a row to open or change it. SETTINGS also holds the trainer (with
cheats on), SAVE PHOTO and the achievements, which have keyboard keys on the PC.

## Troubleshooting

| Symptom | Cause |
| --- | --- |
| The app closes at once | The game files are not on the device: run `push-data`. If logcat says `Cannot create the state folder`, files were copied into the app's folder by hand: run `push-data` again, which lets the app use them. |
| `VULKAN_CAPABILITY_REPORT` in the log | The device's features, formats and memory, logged at every start; attach it to reports. |
| RESOLUTION SCALE offers only 1X | Higher scales need resolve buffers larger than a phone's shared memory holds; `android_allow_resolution_scale` lifts the limit for testing. |
| `skipped a resolve` in the log | A guest copy the renderer cannot pack yet (one is known, in the title screen's attract sequence); the frame continues without it. |
| No sound | No output device could be opened; the game runs silently instead of stopping. |
