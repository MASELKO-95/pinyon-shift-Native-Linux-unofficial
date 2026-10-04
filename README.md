# Pinyon Shift for Linux

Unofficial native Linux port by **MASELKO-95**. Independently maintained at
[the Linux repository](https://github.com/MASELKO-95/pinyon-shift-Native-Linux-unofficial). Based on
[arcanite24/pinyon-shift](https://github.com/arcanite24/pinyon-shift).
Original authorship and BSD-3-Clause licensing are preserved.

See [Linux versus Windows](docs/LINUX_DIFFERENCES.md),
[disc editions and game languages](docs/GAME_EDITIONS.md),
[maintainer notes](docs/LINUX_HANDOFF.md), and [credits](AUTHORS.md).

## Choose a version

Download **PinyonShift-Linux-<version>.tar.gz** from
[Releases](https://github.com/MASELKO-95/pinyon-shift-Native-Linux-unofficial/releases), extract it, and run
`./PinyonShift.sh`. Current version: **1.0.0**.

Choose the ISO dumped from your own supported disc in the launcher. The game
is built locally. No game files, translated game binaries or saves are included.

Requires x86_64 Linux, X11 or XWayland, a Vulkan GPU/driver, Python 3.11.8+,
Git, a C++ build environment and SDL's system development dependencies.
The launcher includes its .NET runtime. First setup needs an internet connection.
For a source-only checkout, build the launcher with
`./tools/build-launcher.sh --download`, then run `./tools/launch-launcher.sh`.

Keep your previous folder when upgrading. Reuse saves with
`./PinyonShift.sh --state-root /absolute/path/to/previous/source/.local/preview`.
Never delete that state folder when replacing the application.

This is an independent Linux port. The Windows launcher and upstream releases
remain available from the original project.
