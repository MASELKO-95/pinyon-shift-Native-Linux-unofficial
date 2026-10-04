# Unofficial Linux port versus upstream Windows

This fork is maintained by **MASELKO-95** at
[pinyon-shift-Native-Linux-unofficial](https://github.com/MASELKO-95/pinyon-shift-Native-Linux-unofficial).
The original project, launcher design and branding are by **arcanite24 and
the Pinyon Shift contributors**. Their BSD-3-Clause license is preserved.
This is an independently maintained port of upstream **0.4.0**.

| Area | Upstream Windows | This Linux port |
| --- | --- | --- |
| Execution | Native Windows x86_64 | Native Linux x86_64; no Wine |
| Renderer | Vulkan or Direct3D 12 | Vulkan; no Direct3D 12 or OpenGL backend |
| Launcher | WPF | Avalonia, with bundled .NET runtime; X11/XWayland |
| Setup/build | PowerShell and Windows tools | Bash, Python, Clang, CMake and Ninja |
| Installation | Windows installation/update workflow | Extract the Linux archive or use a source tree; build locally from your disc |
| Disc selection | Upstream supported-disc manifest | Same retail manifest plus explicit experimental Polish NXE selection; exact size and SHA-256 verification |
| Game language | In-game profile settings | Also selectable before Play; language and country saved together |
| Trainer | F10, separate modded profile | Same trainer and profile isolation; can be enabled before Play |
| Saves | Windows preview state directory | Selected Linux state directory, normally `source/.local/preview`; separate `user` and `user-modded` |
| Diagnostics | Windows crash reporting | POSIX crash reporting and local diagnostic archives |
| SDK | Pinned ShiftGlue | Same pinned SDK plus a reproducible Linux patch and Vulkan Headers/Loader pins |
| Android packaging | Upstream developer APK workflow | No Android-build button in the Linux launcher |
| Updates | Upstream Windows release channel | Versions opens releases from this unofficial fork; no automatic replacement of installed files or saves |

## What the public distribution includes

Only the launcher, required source/build tools, configuration, SDK patch,
branding, credits, licenses and user documentation. Source archives exclude
the Windows launcher implementation, developer tests, benchmark results,
research backlogs and local build state. Shared source files needed by the
Linux build are retained. Dependencies are downloaded from their pinned
upstream locations during setup.

The binary archive adds only the Linux launcher and its dependencies. It does
not include the compiled game, disc images, game assets, generated translations,
saves or captures. The original development checkout is preserved separately.

## Disc editions and languages

See [disc and language selection](GAME_EDITIONS.md). Choosing Polish changes
the game's language; it does not bypass disc verification or make an unknown
executable compatible. The launcher interface itself remains English.

## Trainer

Select **Enable trainer** before Play, then press **F10** in the game. Trainer
progress is stored in `user-modded`; disabling it returns to the original
`user` profile. The first modded profile starts from a copy of the original
profile when one exists. They then progress independently.

Credits apply live once the profile is loaded. Some profile edits, including
wristband changes, take effect on the next profile load as stated in the menu.
The Linux port also retries a free-camera request made before the world's
camera controllers exist, instead of discarding it.

## Performance and remaining qualification

Upstream 0.4.0 includes renderer scheduling, high-resolution and animation
fixes carried into this port. The Linux launcher performs first-start Vulkan
preparation in a private profile, but that opening route cannot cover every
track, material and effect. First-use shader work, loading, high internal
resolutions and missed presentation deadlines can still cause uneven frames.

On the local NVIDIA test machine, the synchronized opening route reached the
Viper scene with a 13.876 ms median and 16.436 ms p99 frame time. The Polish
trainer run confirmed live balances of 1,234,567 and 2,234,567 credits, with
13.882 ms median and 18.089 ms p99. These are short opening-route measurements,
not proof of stutter-free gameplay or a guarantee on other hardware. A cold,
unsynchronized fresh-profile route failed its 55 Hz gate at 52.815 Hz.

For a stutter report, record the location, resolution, FPS limit, whether the
trainer was enabled, and whether the same section still stutters on a second
pass. Keep the local performance CSV and logs. Start with 1× resolution and a
60 fps render limit in F6 to separate GPU load from intermittent stalls.
Do not clear caches or reset a save as a troubleshooting step.

Physical controllers, audio, save/load over multiple sessions and other
distributions still require broader interactive testing. Native Wayland in
the launcher is not claimed; it runs through XWayland on Wayland desktops.
