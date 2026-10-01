#!/usr/bin/env python3
"""Pinyon Shift for Android, driven from the player's PC (AP-6.1).

The package holds the game translated from the player's own disc, so, like
pinyon_shift.exe, it is built on their PC and installed on their own device;
the device never compiles and nothing is uploaded anywhere. The extracted
game files travel from the PC to the device over adb.

  pinyon.py android doctor [--install]   check (or install) the SDK, NDK, JDK
  pinyon.py android build                cross-compile and package the APK
  pinyon.py android package              package already built libraries
  pinyon.py android install              adb install -r the package
  pinyon.py android push-data            copy the extracted game to the device
  pinyon.py android run [--null-gpu] [--route FILE] [-- game arguments]
  pinyon.py android pull-logs            copy the state's logs and crashes back
  pinyon.py android stop                 stop the game on the device

Every command takes --serial for adb when more than one device is connected.
Pinned versions live in config/android-toolchain.json.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import time
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WINDOWS = os.name == "nt"
CONFIG = json.loads((ROOT / "config" / "android-toolchain.json").read_text(encoding="utf-8"))
PACKAGE = CONFIG["package"]
ACTIVITY = CONFIG["activity"]
WORK = ROOT / ".local" / "android"
DEVICE_FILES = f"/sdcard/Android/data/{PACKAGE}/files"
# The libraries the package carries besides the C++ runtime, in load order.
NATIVE_LIBRARIES = (
    "librexruntime.so",
    "librexgpu-fh1.so",
    "libpinyon_shift_SpeechFacade_default.so",
    "libpinyon_shift_XMediaFacade_default.so",
    "libmain.so",
)


class AndroidError(RuntimeError):
    pass


def _exe(name: str) -> str:
    return name + (".exe" if WINDOWS else "")


def _script(name: str) -> str:
    return name + (".bat" if WINDOWS else "")


def sdk_root() -> Path:
    candidates = [os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT")]
    if WINDOWS and os.environ.get("LOCALAPPDATA"):
        candidates.append(str(Path(os.environ["LOCALAPPDATA"]) / "Android" / "Sdk"))
    home = Path.home()
    candidates += [str(home / "Library" / "Android" / "sdk"), str(home / "Android" / "Sdk")]
    for candidate in candidates:
        if candidate and (Path(candidate) / "platform-tools").is_dir():
            return Path(candidate)
    raise AndroidError("the Android SDK was not found; set ANDROID_HOME "
                       "(Android Studio's SDK Manager or the command-line tools install it)")


class Tools:
    def __init__(self) -> None:
        self.sdk = sdk_root()
        pins = CONFIG["android_sdk"]
        self.ndk = self.sdk / "ndk" / pins["ndk"]
        self.build_tools = self.sdk / "build-tools" / pins["build_tools"]
        self.android_jar = self.sdk / "platforms" / pins["platform"] / "android.jar"
        self.adb = self.sdk / "platform-tools" / _exe("adb")
        self.aapt2 = self.build_tools / _exe("aapt2")
        self.d8 = self.build_tools / _script("d8")
        self.zipalign = self.build_tools / _exe("zipalign")
        self.apksigner = self.build_tools / _script("apksigner")
        self.sdkmanager = self.sdk / "cmdline-tools" / "latest" / "bin" / _script("sdkmanager")
        java_home = os.environ.get("JAVA_HOME")
        java_bin = Path(java_home) / "bin" if java_home else None
        self.javac = self._find(java_bin, "javac")
        self.keytool = self._find(java_bin, "keytool")
        host = "windows-x86_64" if WINDOWS else f"{platform.system().lower()}-x86_64"
        self.llvm_bin = self.ndk / "toolchains" / "llvm" / "prebuilt" / host / "bin"
        self.strip = self.llvm_bin / _exe("llvm-strip")
        self.libcxx = (self.ndk / "toolchains" / "llvm" / "prebuilt" / host / "sysroot" / "usr"
                       / "lib" / "aarch64-linux-android" / "libc++_shared.so")
        local_cmake = (ROOT / ".local" / "toolchain" / "cmake-3.31.10-windows-x86_64" / "bin"
                       / "cmake.exe")
        self.cmake = Path(str(local_cmake if local_cmake.is_file() else shutil.which("cmake")))
        self.ninja = self._ninja()

    @staticmethod
    def _find(directory: Path | None, name: str) -> Path | None:
        if directory and (directory / _exe(name)).is_file():
            return directory / _exe(name)
        found = shutil.which(name)
        return Path(found) if found else None

    @staticmethod
    def _ninja() -> Path | None:
        found = shutil.which("ninja")
        if found:
            return Path(found)
        if WINDOWS:
            vswhere = (Path(os.environ.get("ProgramFiles(x86)", "")) / "Microsoft Visual Studio"
                       / "Installer" / "vswhere.exe")
            if vswhere.is_file():
                vs = subprocess.run([str(vswhere), "-products", "*", "-latest", "-property",
                                     "installationPath"], capture_output=True, text=True)
                ninja = (Path(vs.stdout.strip()) / "Common7" / "IDE" / "CommonExtensions"
                         / "Microsoft" / "CMake" / "Ninja" / "ninja.exe")
                if ninja.is_file():
                    return ninja
        return None

    def missing(self) -> list[str]:
        required = {
            "NDK " + CONFIG["android_sdk"]["ndk"]: self.ndk / "build" / "cmake"
            / "android.toolchain.cmake",
            "build-tools " + CONFIG["android_sdk"]["build_tools"]: self.aapt2,
            CONFIG["android_sdk"]["platform"]: self.android_jar,
            "platform-tools (adb)": self.adb,
            "JDK " + str(CONFIG["jdk_major"]) + " (javac)": self.javac,
            "JDK keytool": self.keytool,
            "CMake": self.cmake,
            "Ninja": self.ninja,
        }
        return [name for name, path in required.items() if not path or not Path(path).exists()]


def run(command: list, **kwargs) -> subprocess.CompletedProcess:
    completed = subprocess.run([str(part) for part in command], **kwargs)
    if completed.returncode:
        raise AndroidError(f"{Path(str(command[0])).name} failed ({completed.returncode})")
    return completed


def adb(tools: Tools, args: argparse.Namespace, *command: str, **kwargs):
    prefix = [tools.adb] + (["-s", args.serial] if args.serial else [])
    return run(prefix + list(command), **kwargs)


def build_directory(configuration: str) -> Path:
    return ROOT / "out" / "build" / f"android-arm64-{configuration.lower()}"


def release_version() -> tuple[str, int]:
    release = json.loads((ROOT / "config" / "release.json").read_text(encoding="utf-8"))
    name = str(release["version"])
    major, minor, patch = (int(part) for part in name.split("-")[0].split(".")[:3])
    return name, major * 10000 + minor * 100 + patch


def doctor(args: argparse.Namespace) -> int:
    tools = Tools()
    missing = tools.missing()
    if missing and args.install:
        if not tools.sdkmanager.is_file():
            raise AndroidError("the SDK's command-line tools are needed to install packages")
        packages = CONFIG["android_sdk"]["packages"]
        run([tools.sdkmanager, "--install", *packages], input="y\n" * 8, text=True)
        missing = Tools().missing()
    for line in (f"sdk: {tools.sdk}", f"ndk: {tools.ndk}", f"build-tools: {tools.build_tools}",
                 f"javac: {tools.javac}", f"cmake: {tools.cmake}", f"ninja: {tools.ninja}"):
        print(line)
    if missing:
        print("missing: " + ", ".join(missing), file=sys.stderr)
        return 1
    print("ready")
    return 0


def build(args: argparse.Namespace) -> int:
    tools = Tools()
    missing = tools.missing()
    if missing:
        raise AndroidError("missing " + ", ".join(missing) + " (pinyon.py android doctor --install)")
    if not (ROOT / ".local" / "generated" / "default" / "sources.cmake").is_file():
        raise AndroidError("the generated game code is missing; build the game on this PC "
                           "first (the launcher or tools/build-preview.ps1)")
    directory = build_directory(args.configuration)
    environment = dict(os.environ, ANDROID_NDK_HOME=str(tools.ndk))
    if not (directory / "CMakeCache.txt").is_file():
        run([tools.cmake, "--preset", f"android-arm64-{args.configuration.lower()}",
             f"-DCMAKE_MAKE_PROGRAM={tools.ninja}", f"-DPYTHON_EXECUTABLE={sys.executable}",
             f"-DPython3_EXECUTABLE={sys.executable}"], cwd=ROOT, env=environment)
    command = [tools.cmake, "--build", directory, "--target", "pinyon_shift"]
    if args.jobs:
        command += ["-j", str(args.jobs)]
    run(command, cwd=ROOT, env=environment)
    return package(args, tools)


def _keystore(tools: Tools) -> Path:
    """A signing key made on this PC for this player's own installs."""
    keystore = WORK / "signing" / "pinyon-shift-local.keystore"
    if not keystore.is_file():
        keystore.parent.mkdir(parents=True, exist_ok=True)
        run([tools.keytool, "-genkeypair", "-keystore", keystore, "-storepass", "pinyon-local",
             "-keypass", "pinyon-local", "-alias", "pinyon-local", "-keyalg", "RSA",
             "-keysize", "3072", "-validity", "10000", "-dname", "CN=Pinyon Shift local build"],
            stdout=subprocess.DEVNULL)
    return keystore


def package(args: argparse.Namespace, tools: Tools | None = None) -> int:
    tools = tools or Tools()
    directory = build_directory(args.configuration)
    libraries = []
    for name in NATIVE_LIBRARIES:
        found = list(directory.glob(f"**/{name}"))
        found = [path for path in found if "CMakeFiles" not in path.parts]
        if not found:
            raise AndroidError(f"{name} is not built in {directory}")
        libraries.append(max(found, key=lambda path: path.stat().st_mtime))
    libraries.append(tools.libcxx)

    staging = WORK / "package"
    if staging.exists():
        shutil.rmtree(staging)
    (staging / "classes").mkdir(parents=True)
    (staging / "dex").mkdir()
    (staging / "lib").mkdir()

    version_name, version_code = release_version()
    manifest = (ROOT / "android" / "AndroidManifest.xml").read_text(encoding="utf-8")
    manifest = manifest.replace("<manifest ", f'<manifest package="{PACKAGE}" ', 1)
    (staging / "AndroidManifest.xml").write_text(manifest, encoding="utf-8")
    unsigned = staging / "unsigned.apk"
    run([tools.aapt2, "link", "-o", unsigned, "-I", tools.android_jar,
         "--manifest", staging / "AndroidManifest.xml",
         "--min-sdk-version", str(CONFIG["min_sdk"]),
         "--target-sdk-version", str(CONFIG["target_sdk"]),
         "--version-name", version_name, "--version-code", str(version_code)])

    sdl_java = ROOT / "thirdparty" / "shiftglue-sdk" / "thirdparty" / "sdl3" / "android-project" \
        / "app" / "src" / "main" / "java"
    sources = sorted(sdl_java.rglob("*.java")) + sorted((ROOT / "android" / "java").rglob("*.java"))
    run([tools.javac, "-source", "1.8", "-target", "1.8", "-nowarn", "-Xlint:-options",
         "-bootclasspath", tools.android_jar, "-classpath", tools.android_jar,
         "-encoding", "UTF-8", "-d", staging / "classes", *sources])
    classes = sorted((staging / "classes").rglob("*.class"))
    run([tools.d8, "--release", "--min-api", str(CONFIG["min_sdk"]), "--lib", tools.android_jar,
         "--output", staging / "dex", *classes])

    # The device gets libraries without debug sections; the PC keeps the
    # originals for symbolizing crash reports.
    stripped = []
    for library in libraries:
        target = staging / "lib" / library.name
        run([tools.strip, "--strip-debug", "-o", target, library])
        stripped.append(target)

    with zipfile.ZipFile(unsigned, "a", compression=zipfile.ZIP_DEFLATED) as apk:
        apk.write(staging / "dex" / "classes.dex", "classes.dex")
        for library in stripped:
            apk.write(library, f"lib/{CONFIG['abi']}/{library.name}")

    aligned = staging / "aligned.apk"
    run([tools.zipalign, "-P", "16", "-f", "4", unsigned, aligned])
    output = WORK / "pinyon-shift.apk"
    keystore = _keystore(tools)
    run([tools.apksigner, "sign", "--ks", keystore, "--ks-pass", "pass:pinyon-local",
         "--key-pass", "pass:pinyon-local", "--out", output, aligned])
    size = output.stat().st_size
    print(json.dumps({"apk": str(output), "bytes": size, "version": version_name,
                      "libraries": [path.name for path in stripped]}))
    return 0


def install(args: argparse.Namespace) -> int:
    tools = Tools()
    apk = WORK / "pinyon-shift.apk"
    if not apk.is_file():
        raise AndroidError("build the package first (pinyon.py android build)")
    adb(tools, args, "install", "-r", apk)
    return 0


def push_data(args: argparse.Namespace) -> int:
    """The extracted game, pushed with --sync so an interrupted copy resumes
    and an unchanged file is not sent again."""
    tools = Tools()
    game = (args.game_root or ROOT / ".local" / "game" / "base").resolve()
    if not (game / "default.xex").is_file():
        raise AndroidError(f"the extracted game is not at {game}")
    adb(tools, args, "shell", "mkdir", "-p", f"{DEVICE_FILES}/game/base", f"{DEVICE_FILES}/state")
    adb(tools, args, "push", "--sync", f"{game}{os.sep}.", f"{DEVICE_FILES}/game/base")
    return 0


def _pid(tools: Tools, args: argparse.Namespace) -> str:
    completed = subprocess.run([str(tools.adb)] + (["-s", args.serial] if args.serial else [])
                               + ["shell", "pidof", PACKAGE], capture_output=True, text=True)
    return completed.stdout.strip()


def run_game(args: argparse.Namespace) -> int:
    tools = Tools()
    extras: list[str] = []
    game_arguments = list(args.game_arguments)
    if args.null_gpu:
        game_arguments.append("--gpu_backend=null")
    if args.route:
        remote_route = f"{DEVICE_FILES}/state/render-tests/{args.route.name}"
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        remote_output = f"{DEVICE_FILES}/state/render-test-output/{args.route.stem}-{stamp}"
        adb(tools, args, "shell", "mkdir", "-p", f"{DEVICE_FILES}/state/render-tests")
        adb(tools, args, "push", args.route.resolve(), remote_route, stdout=subprocess.DEVNULL)
        extras += ["--es", "env.PINYON_SHIFT_FH1_RENDER_TEST_SCRIPT", remote_route,
                   "--es", "env.PINYON_SHIFT_FH1_RENDER_TEST_OUTPUT", remote_output]
        game_arguments.append("--pinyon_shift_skip_opening_movies=true")
        print(f"route output: {remote_output}")
    if game_arguments:
        extras += ["--esa", "args", ",".join(game_arguments)]
    adb(tools, args, "shell", "am", "start", "-S", "-W", "-n", f"{PACKAGE}/{ACTIVITY}", *extras,
        stdout=subprocess.DEVNULL)
    if not args.wait:
        return 0
    deadline = time.monotonic() + args.timeout
    time.sleep(3)
    while _pid(tools, args):
        if time.monotonic() > deadline:
            adb(tools, args, "shell", "am", "force-stop", PACKAGE)
            raise AndroidError(f"timed out after {args.timeout} seconds")
        time.sleep(2)
    return 0


def stop(args: argparse.Namespace) -> int:
    adb(Tools(), args, "shell", "am", "force-stop", PACKAGE)
    return 0


def pull_logs(args: argparse.Namespace) -> int:
    tools = Tools()
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    destination = (args.output or WORK / "device-logs" / stamp).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    for folder in ("logs", "crashes", "reports", "render-test-output"):
        subprocess.run([str(tools.adb)] + (["-s", args.serial] if args.serial else [])
                       + ["pull", f"{DEVICE_FILES}/state/{folder}", str(destination)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with (destination / "logcat.txt").open("w", encoding="utf-8", errors="replace") as stream:
        subprocess.run([str(tools.adb)] + (["-s", args.serial] if args.serial else [])
                       + ["logcat", "-d", "-b", "main,crash"], stdout=stream)
    print(destination)
    return 0


def add_parser(commands) -> None:
    android = commands.add_parser("android", help="build and run the game on an Android device")
    sub = android.add_subparsers(dest="android_command", required=True)

    def command(name: str, handler, help_text: str) -> argparse.ArgumentParser:
        parser = sub.add_parser(name, help=help_text)
        parser.add_argument("--serial", help="adb device serial")
        parser.add_argument("--configuration", choices=("Release", "RelWithDebInfo"),
                            default="Release")
        parser.set_defaults(android_handler=handler)
        return parser

    parser = command("doctor", doctor, "check the Android toolchain")
    parser.add_argument("--install", action="store_true", help="install missing SDK packages")
    parser = command("build", build, "cross-compile the game and package the APK")
    parser.add_argument("--jobs", type=int)
    command("package", package, "package already built libraries into the APK")
    command("install", install, "install the APK on the device")
    parser = command("push-data", push_data, "copy the extracted game files to the device")
    parser.add_argument("--game-root", type=Path)
    parser = command("run", run_game, "start the game on the device")
    parser.add_argument("--null-gpu", action="store_true", help="no renderer (gpu_backend=null)")
    parser.add_argument("--route", type=Path, help="a render-test route to run")
    parser.add_argument("--wait", action="store_true", help="wait until the game exits")
    parser.add_argument("--timeout", type=float, default=1800)
    parser.add_argument("game_arguments", nargs="*", help="after --, passed to the game")
    command("stop", stop, "stop the game on the device")
    parser = command("pull-logs", pull_logs, "copy logs, crashes and route output to the PC")
    parser.add_argument("--output", type=Path)


def main(args: argparse.Namespace) -> int:
    try:
        return args.android_handler(args)
    except AndroidError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    top = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    add_parser(top.add_subparsers(dest="command", required=True))
    sys.exit(main(top.parse_args()))
