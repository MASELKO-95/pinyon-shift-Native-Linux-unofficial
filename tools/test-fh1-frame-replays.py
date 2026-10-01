#!/usr/bin/env python3
"""Replay every FH1 frame dump in a directory against its golden replay.

The offline executor regression suite: each `*.fh1frame` needs a
`<dump>.native.golden.bin` from a known-good build (`--write-golden`
records them). Dumps are local (they hold guest memory) and are not committed.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path


def load_replay():
    path = Path(__file__).with_name("replay-fh1-frame.py")
    spec = importlib.util.spec_from_file_location("replay_fh1_frame", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("directory", type=Path)
    parser.add_argument("--state-root", type=Path, required=True)
    parser.add_argument("--shader-pack", type=Path)
    parser.add_argument("--configuration")
    parser.add_argument("--build-directory", type=Path)
    parser.add_argument("--game-argument", action="append",
                        help="passed to the game, such as --gpu_backend=vulkan")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--write-golden", action="store_true")
    args = parser.parse_args()
    replay = load_replay()
    dumps = sorted(args.directory.glob("*.fh1frame"))
    if not dumps:
        print(f"error: no frame dumps in {args.directory}", file=sys.stderr)
        return 1
    failures = 0
    for dump in dumps:
        options = argparse.Namespace(
            dump=dump, state_root=args.state_root, work=Path(".local/replay/state"),
            shader_pack=args.shader_pack,
            configuration=args.configuration, build_directory=args.build_directory,
            hidden=True, timeout=args.timeout, max_differing_words=None,
            write_golden=args.write_golden, game_argument=args.game_argument)
        try:
            result = replay.replay(options)
        except (OSError, ValueError, RuntimeError) as error:
            result = {"failure": str(error)}
        if not args.write_golden and "golden_differing_words" not in result:
            result.setdefault("failure", "no golden replay")
        failures += "failure" in result
        print(json.dumps({"dump": dump.name, **result}))
    print(f"{len(dumps) - failures}/{len(dumps)} frame replays passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
