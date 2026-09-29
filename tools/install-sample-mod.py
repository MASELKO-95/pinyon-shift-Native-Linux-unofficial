#!/usr/bin/env python3
"""Install a sample mod into a state directory and enable it (NP-7.6).

  install-sample-mod.py <state-root> hello_telemetry [--build-dir DIR]
  install-sample-mod.py <state-root> english_strings [--game-root DIR]

hello_telemetry needs the pinyon_shift_mod_hello_telemetry target built.
english_strings copies the player's own media/StringTables/EN.zip into the
mod's game/ tree. The mod is appended to enabled_mods in the state's
pinyon_shift.toml. Refuses the AppData save: pass a private state copy.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SAMPLES = ROOT / "mods_src" / "samples"


def enable(config: Path, name: str) -> None:
    text = config.read_text(encoding="utf-8") if config.exists() else ""
    match = re.search(r'^enabled_mods[ \t]*=[ \t]*"([^"]*)"[ \t]*$', text, re.MULTILINE)
    mods = [m for m in (match.group(1).split(",") if match else []) if m.strip()]
    if name not in mods:
        mods.append(name)
    line = f'enabled_mods = "{",".join(mods)}"'
    if match:
        text = text[:match.start()] + line + text[match.end():]
    else:
        text = text.rstrip("\n") + ("\n" if text else "") + line + "\n"
    config.write_text(text, encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("state_root", type=Path)
    parser.add_argument("mod", choices=["hello_telemetry", "english_strings"])
    parser.add_argument("--build-dir", type=Path,
                        default=ROOT / "out" / "build" / "win-amd64-release")
    parser.add_argument("--game-root", type=Path, default=ROOT / ".local" / "game" / "base")
    args = parser.parse_args()
    state = args.state_root.resolve()
    appdata = Path(os.environ.get("LOCALAPPDATA", "")) / "PinyonShift"
    if appdata.parts and str(state).lower().startswith(str(appdata.resolve()).lower()):
        print("refusing to install into the AppData save; use a private state copy")
        return 1
    target = state / "mods" / args.mod
    target.mkdir(parents=True, exist_ok=True)
    shutil.copy2(SAMPLES / args.mod / "mod.toml", target / "mod.toml")
    if args.mod == "hello_telemetry":
        library = args.build_dir / "mods" / "hello_telemetry" / "code" / "hello_telemetry.dll"
        if not library.exists():
            print(f"{library} is missing; build pinyon_shift_mod_hello_telemetry first")
            return 1
        (target / "code").mkdir(exist_ok=True)
        shutil.copy2(library, target / "code" / "hello_telemetry.dll")
    else:
        source = args.game_root / "media" / "StringTables" / "EN.zip"
        destination = target / "game" / "media" / "StringTables" / "EN.zip"
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
    enable(state / "config" / "pinyon_shift.toml", args.mod)
    print(f"installed {args.mod} into {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
