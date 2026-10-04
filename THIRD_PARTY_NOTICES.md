# Third-party notices

This unofficial Linux port retains the original Pinyon Shift BSD-3-Clause
license and authorship. See [AUTHORS.md](AUTHORS.md) and [LICENSE](LICENSE).

Pinyon Shift downloads the following third-party projects during local setup.
Their own licenses apply; they are not relicensed by this repository.

- [ShiftGlue SDK](https://github.com/arcanite24/shiftglue-sdk), the pinned
  ReXGlue/Xenia-derived fork, including its BSD license and dependency notices
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)
- [LLVM](https://github.com/llvm/llvm-project)
- [extract-xiso](https://github.com/XboxDev/extract-xiso)
- [Git for Windows](https://github.com/git-for-windows/git)
- [Microsoft Visual Studio Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/)

Exact versions, source URLs, and archive hashes are recorded in
`config/release-toolchain.json` and `config/release-toolchain-linux.json`.
Linux source pins and the SDK patch are recorded in `config/linux-port.json`.
The patch retains the affected SDK files' original licenses. Khronos Vulkan
Headers and Loader retain their own Apache-2.0/MIT license files in their
submodule source trees. The Linux build uses the system Vulkan loader/driver.
LLVM retains its Apache-2.0-with-LLVM-exception notices; extract-xiso retains
its upstream license. Downloaded tools are not included in the launcher archive. ReXGlue's transitive dependencies are fetched
by its pinned source tree and retain their upstream notices.

## Linux graphical launcher

The C# Linux launcher uses Avalonia (MIT), MicroCom.Runtime (MIT),
Tmds.DBus.Protocol (MIT), SkiaSharp and HarfBuzzSharp (MIT, with their native
components' notices), and the .NET runtime (MIT and third-party notices).
Versions and NuGet content hashes are locked in
`launcher/PinyonShift.Launcher.Linux/packages.lock.json`; the bootstrap SDK URL
and SHA-512 are in `config/launcher-toolchain-linux.json`.

Avalonia, MicroCom and D-Bus license texts are retained in the launcher's
`Notices` directory. The build script also copies the restored packages'
license/notice files and the project's BSD license into the published output.
Sources: [Avalonia](https://github.com/AvaloniaUI/Avalonia),
[MicroCom](https://github.com/kekekeks/MicroCom),
[Tmds.DBus](https://github.com/tmds/Tmds.DBus),
[SkiaSharp](https://github.com/mono/SkiaSharp),
[.NET runtime](https://github.com/dotnet/runtime).
