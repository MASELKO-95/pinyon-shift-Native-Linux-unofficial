"""Snapshot an FH1 preview save into a pinned render-test seed.

The seed holds copies of the save's `user` and `config` directories plus the
FH1 shader catalogs, so scripted routes keep reaching the same event while the
live save progresses. The source is only read. Run routes from the seed with
`tools/run-fh1-render-test.py --state-root <seed>`; that runner copies the
seed again for every run, so the seed itself is never written either.
"""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


CATALOGS = (
    "fh1-native-shaders-v2.bin",
    "fh1-native-pipelines-v1.bin",
    "fh1-gpu-prewarm-v3.txt",
)
SEED_ROOT = Path(".local/render-seeds")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def game_running() -> bool:
    if sys.platform != "win32":
        return False
    listing = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq pinyon_shift.exe", "/NH"],
        capture_output=True,
        text=True,
        check=False,
    )
    return "pinyon_shift.exe" in listing.stdout


def create_seed(source: Path, destination: Path, note: str = "") -> dict:
    source = source.resolve()
    profiles = sorted((source / "user").glob("**/ForzaProfile/ForzaProfile"))
    if not profiles:
        raise ValueError(f"no FH1 profile below {source / 'user'}")
    if destination.exists():
        raise ValueError(f"refusing to overwrite existing seed: {destination}")
    missing = [name for name in CATALOGS if not (source / "cache" / name).is_file()]
    if missing:
        raise ValueError("missing FH1 shader catalogs: " + ", ".join(missing))

    destination.mkdir(parents=True)
    try:
        for name in ("user", "config"):
            if (source / name).is_dir():
                shutil.copytree(source / name, destination / name)
        (destination / "cache").mkdir()
        for name in CATALOGS:
            shutil.copy2(source / "cache" / name, destination / "cache" / name)
        manifest = {
            "schema": "pinyon-shift.render-seed.v1",
            "created_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "source": str(source),
            "note": note,
            "profiles": {
                str(path.relative_to(source)).replace("\\", "/"): sha256(path)
                for path in profiles
            },
            "catalogs": {
                name: sha256(destination / "cache" / name) for name in CATALOGS
            },
        }
        for relative, digest in manifest["profiles"].items():
            if sha256(destination / relative) != digest:
                raise RuntimeError(f"profile changed while copying: {relative}")
        (destination / "seed.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
        )
    except BaseException:
        shutil.rmtree(destination, ignore_errors=True)
        raise
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("name", help="seed directory name under .local/render-seeds")
    parser.add_argument("--state-root", type=Path, required=True,
                        help="preview state root that holds the save to snapshot")
    parser.add_argument("--note", default="",
                        help="where the save stands, e.g. the next event it reaches")
    args = parser.parse_args()
    if game_running():
        print("Pinyon Shift is running; close it so the save is consistent.",
              file=sys.stderr)
        return 1
    manifest = create_seed(args.state_root, SEED_ROOT / args.name, args.note)
    print(json.dumps({"seed": str((SEED_ROOT / args.name).resolve()), **manifest}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
