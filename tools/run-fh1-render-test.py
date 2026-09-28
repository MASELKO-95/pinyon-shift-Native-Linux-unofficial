#!/usr/bin/env python3
"""Run one deterministic FH1 renderer scenario without UI automation."""

from __future__ import annotations

import argparse
import json
import math
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


SCHEMA = "pinyon-shift.fh1-render-test-result.v1"
HEADER = "pinyon-shift-fh1-render-test-v1"
NATIVE_COUNTER = re.compile(
    r"FH1 (?:V5 )?native (?P<family>.+?) (?:draws|vertex draws|clears) (?P<count>\d+)"
)
PASS_FAMILY = re.compile(
    r"FH1 V5 pass family (?P<family>[0-9A-F]{16}): attachment "
    r"(?P<attachment>[0-9A-F]{16}), first family (?P<first_family>[0-9A-F]{16}), "
    r"first draw (?P<first_draw>[0-9A-F]{16}), copy (?P<copy>[0-9A-F]{16}), "
    r"samples (?P<samples>\d+), draws (?P<minimum>\d+)-(?P<maximum>\d+) "
    r"\(average (?P<average_draws>\d+)\), total (?P<total_ns>\d+) ns, "
    r"average (?P<average_ns>\d+) ns, maximum (?P<maximum_ns>\d+) ns"
    r"(?:, average draw (?P<average_draw_ns>\d+) ns, average resolve "
    r"(?P<average_resolve_ns>\d+) ns, maximum resolve "
    r"(?P<maximum_resolve_ns>\d+) ns)?"
)
DEFAULT_IMAGE_LIMITS = (12.0, 30.0, 0.60)


def prepare_isolated_state(source: Path, destination: Path) -> None:
    destination.mkdir(parents=True)
    for name in ("user", "config"):
        source_directory = source / name
        if source_directory.is_dir():
            shutil.copytree(source_directory, destination / name)


def resolve_disc_shader_corpus(path: Path) -> Path:
    for candidate in (path, path / "ucode"):
        if candidate.is_dir() and next(candidate.glob("*.bin"), None):
            return candidate.resolve()
    raise ValueError(
        "--disc-shader-corpus-dir contains no .bin shaders (directly or in ucode/)"
    )


def seed_fh1_shader_storage(source: Path, destination: Path) -> list[str]:
    source_directory = source / "cache"
    destination_directory = destination / "cache"
    names = ("fh1-native-shaders-v2.bin", "fh1-native-pipelines-v1.bin")
    missing = [name for name in names if not (source_directory / name).is_file()]
    if missing:
        raise ValueError("missing FH1 shader storage: " + ", ".join(missing))
    destination_directory.mkdir(parents=True, exist_ok=True)
    for name in names:
        shutil.copy2(source_directory / name, destination_directory / name)
    return list(names)


def seed_fh1_pipeline_prewarm(source: Path, destination: Path) -> str:
    name = "fh1-gpu-prewarm-v3.txt"
    source_path = source / "cache" / name
    if not source_path.is_file():
        raise ValueError(f"missing FH1 pipeline prewarm allowlist: {source_path}")
    destination_path = destination / "cache" / name
    destination_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source_path, destination_path)
    return name


def require_image_reference(image_limits, baseline_dir, record_baseline):
    if image_limits and not baseline_dir and not record_baseline:
        raise ValueError(
            "scenario has image expectations; pass --baseline-dir or "
            "--record-baseline"
        )


def compare_vehicle_poses(captures, baseline_captures, maximum_distance):
    baseline_by_frame = {capture["frame"]: capture for capture in baseline_captures}
    comparisons = []
    for capture in captures:
        baseline = baseline_by_frame.get(capture["frame"])
        if not baseline or "vehicle_pose" not in capture or "vehicle_pose" not in baseline:
            raise RuntimeError(
                f"missing vehicle pose baseline for frame {capture['frame']}"
            )
        distance = math.dist(
            (capture["vehicle_pose"][axis] for axis in ("x", "y", "z")),
            (baseline["vehicle_pose"][axis] for axis in ("x", "y", "z")),
        )
        comparisons.append(
            {"frame": capture["frame"], "distance": round(distance, 6)}
        )
        if distance > maximum_distance:
            raise RuntimeError(
                f"vehicle pose at frame {capture['frame']} differs by "
                f"{distance:.3f} m (maximum {maximum_distance:.3f} m)"
            )
    return comparisons


def parse_scenario(
    path: Path,
) -> tuple[
    list[tuple[int, str]],
    int,
    set[str],
    dict[str, tuple[float, float, float]],
    tuple[float, float, float, float] | None,
    float | None,
    tuple[float, float, int] | None,
    list[tuple[str, str, float]],
    set[str],
    list[set[str]],
]:
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[0] != HEADER:
        raise ValueError("unsupported FH1 render-test schema")
    captures: list[tuple[int, str]] = []
    stop = 0
    required_native: set[str] = set()
    image_limits: dict[str, tuple[float, float, float]] = {}
    performance_limits = None
    distinct_presentation_min = None
    simulation_time_limits = None
    capture_mae_minimums: list[tuple[str, str, float]] = []
    race_hud_captures: set[str] = set()
    race_hud_any_groups: list[set[str]] = []
    previous_input = -1
    first_input = None
    for number, line in enumerate(lines[1:], 2):
        if line.startswith("# require-native "):
            required_native.add(line.removeprefix("# require-native ").strip())
            continue
        if line.startswith("# expect-image "):
            fields = line.split()
            if len(fields) != 6:
                raise ValueError(f"line {number}: invalid image expectation")
            limits = tuple(float(value) for value in fields[3:])
            if any(value < 0 for value in limits) or limits[2] > 1:
                raise ValueError(f"line {number}: invalid image limits")
            image_limits[fields[2]] = limits
            continue
        if line.startswith("# expect-performance "):
            fields = line.split()
            if len(fields) != 6 or performance_limits is not None:
                raise ValueError(f"line {number}: invalid performance expectation")
            performance_limits = tuple(float(value) for value in fields[2:])
            if any(value < 0 for value in performance_limits):
                raise ValueError(f"line {number}: invalid performance limits")
            continue
        if line.startswith("# expect-distinct-presentation "):
            fields = line.split()
            if len(fields) != 3 or distinct_presentation_min is not None:
                raise ValueError(
                    f"line {number}: invalid distinct-presentation expectation"
                )
            distinct_presentation_min = float(fields[2])
            if distinct_presentation_min < 0:
                raise ValueError(
                    f"line {number}: invalid distinct-presentation minimum"
                )
            continue
        if line.startswith("# expect-simulation-time "):
            fields = line.split()
            if len(fields) != 5 or simulation_time_limits is not None:
                raise ValueError(
                    f"line {number}: invalid simulation-time expectation"
                )
            simulation_time_limits = (
                float(fields[2]),
                float(fields[3]),
                int(fields[4]),
            )
            if (
                simulation_time_limits[0] < 0
                or simulation_time_limits[0] > simulation_time_limits[1]
                or simulation_time_limits[2] < 0
            ):
                raise ValueError(
                    f"line {number}: invalid simulation-time limits"
                )
            continue
        if line.startswith("# expect-capture-mae "):
            fields = line.split()
            if len(fields) != 5:
                raise ValueError(f"line {number}: invalid capture-MAE expectation")
            minimum = float(fields[4])
            if minimum < 0:
                raise ValueError(f"line {number}: invalid capture-MAE minimum")
            capture_mae_minimums.append((fields[2], fields[3], minimum))
            continue
        if line.startswith("# expect-race-hud "):
            fields = line.split()
            if len(fields) != 3:
                raise ValueError(f"line {number}: invalid race-HUD expectation")
            race_hud_captures.add(fields[2])
            continue
        if line.startswith("# expect-race-hud-any "):
            fields = line.split()
            if len(fields) < 4:
                raise ValueError(f"line {number}: invalid race-HUD-any expectation")
            race_hud_any_groups.append(set(fields[2:]))
            continue
        if not line or line.startswith("#"):
            continue
        fields = line.split()
        if fields[0] == "input" and len(fields) == 9:
            frame = int(fields[1])
            if frame <= previous_input:
                raise ValueError(f"line {number}: input frames must increase")
            int(fields[2], 16)
            values = [int(value) for value in fields[3:]]
            if not 0 <= values[0] <= 255 or not 0 <= values[1] <= 255:
                raise ValueError(f"line {number}: trigger is out of range")
            if any(value < -32768 or value > 32767 for value in values[2:]):
                raise ValueError(f"line {number}: stick is out of range")
            previous_input = frame
            if first_input is None:
                first_input = frame
        elif fields[0] == "wait" and (
            (len(fields) == 4 and fields[3] == "vehicle")
            or (len(fields) == 5 and fields[3] in ("vehicle-moved", "movie", "file"))
        ):
            # wait <frame> <max-frames> <condition> [argument]: the script
            # clock holds at <frame> until the game reaches the condition.
            if int(fields[1]) <= 0 or int(fields[2]) <= 0:
                raise ValueError(f"line {number}: invalid wait")
            if fields[3] == "vehicle-moved" and int(fields[4]) <= 0:
                raise ValueError(f"line {number}: invalid wait distance")
        elif fields[0] == "capture" and len(fields) == 3:
            captures.append((int(fields[1]), fields[2]))
        elif fields[0] == "stop" and len(fields) == 2:
            if stop:
                raise ValueError("scenario has multiple stop commands")
            stop = int(fields[1])
        else:
            raise ValueError(f"line {number}: invalid command")
    if previous_input < 0 or first_input != 0:
        raise ValueError("scenario must start with input frame 0")
    if not captures or any(
        frame <= 0 or (index and frame <= captures[index - 1][0])
        for index, (frame, _) in enumerate(captures)
    ):
        raise ValueError("capture frames must be positive and increasing")
    if stop <= captures[-1][0]:
        raise ValueError("stop frame must follow every capture")
    unknown_images = image_limits.keys() - {name for _, name in captures}
    if unknown_images:
        raise ValueError("image expectation has no capture: " + min(unknown_images))
    capture_names = {name for _, name in captures}
    unknown_race_hud = race_hud_captures - capture_names
    unknown_race_hud.update(
        name for group in race_hud_any_groups for name in group - capture_names
    )
    if unknown_race_hud:
        raise ValueError(
            "race-HUD expectation names an unknown capture: "
            + min(unknown_race_hud)
        )
    for first, second, _ in capture_mae_minimums:
        if first not in capture_names or second not in capture_names:
            raise ValueError("capture-MAE expectation names an unknown capture")
    if performance_limits and performance_limits[2] > performance_limits[3]:
        raise ValueError("simulation cadence limits are reversed")
    return (
        captures,
        stop,
        required_native,
        image_limits,
        performance_limits,
        distinct_presentation_min,
        simulation_time_limits,
        capture_mae_minimums,
        race_hud_captures,
        race_hud_any_groups,
    )


def ppm_payload(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
    if not match:
        raise ValueError(f"{path}: invalid PPM")
    width, height = map(int, match.groups())
    pixels = data[match.end() :]
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path}: truncated PPM")
    return width, height, pixels


def first_race_hud_summary(output: Path, names: set[str]):
    for name in sorted(names):
        try:
            return name, race_hud_summary(output / f"{name}.ppm")
        except RuntimeError:
            pass
    raise RuntimeError(
        "missing FH1 race HUD in every alternative capture: "
        + ", ".join(sorted(names))
    )


def ppm_summary(path: Path) -> dict[str, object]:
    width, height, pixels = ppm_payload(path)
    minimum, maximum = min(pixels), max(pixels)
    mean = sum(pixels) / len(pixels)
    if maximum - minimum < 8 or mean < 1 or mean > 254:
        raise ValueError(f"{path}: blank or degenerate output")
    return {
        "file": str(path),
        "width": width,
        "height": height,
        "minimum": minimum,
        "maximum": maximum,
        "mean": round(mean, 3),
    }


def compare_capture_mae(output: Path, first: str, second: str) -> float:
    first_width, first_height, first_pixels = ppm_payload(output / f"{first}.ppm")
    second_width, second_height, second_pixels = ppm_payload(output / f"{second}.ppm")
    if (first_width, first_height) != (second_width, second_height):
        raise RuntimeError(f"capture dimensions differ: {first}, {second}")
    return sum(abs(a - b) for a, b in zip(first_pixels, second_pixels)) / len(
        first_pixels
    )


def race_hud_summary(path: Path) -> dict[str, float]:
    width, height, pixels = ppm_payload(path)

    def fraction(bounds, predicate):
        x0, x1, y0, y1 = bounds
        matches = total = 0
        for y in range(int(height * y0), int(height * y1)):
            for x in range(int(width * x0), int(width * x1)):
                offset = (y * width + x) * 3
                red, green, blue = pixels[offset : offset + 3]
                matches += predicate(red, green, blue)
                total += 1
        return matches / total

    white = lambda red, green, blue: min(red, green, blue) > 220
    pink = lambda red, green, blue: (
        red > 170 and blue > 70 and red > green * 1.35
    )
    result = {
        "lap_white_fraction": fraction((0.03, 0.23, 0.02, 0.14), white),
        "place_white_fraction": fraction((0.78, 0.97, 0.02, 0.14), white),
        "standings_white_fraction": fraction((0.78, 0.97, 0.15, 0.40), white),
        "standings_pink_fraction": fraction((0.78, 0.97, 0.24, 0.40), pink),
    }
    if (result["lap_white_fraction"] < 0.01 or
            result["place_white_fraction"] < 0.01 or
            max(result["standings_white_fraction"],
                result["standings_pink_fraction"]) < 0.01):
        raise RuntimeError(f"missing FH1 race HUD in {path.name}: {result}")
    return result


def load_events(path: Path) -> list[dict[str, object]]:
    events = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            raise ValueError(f"{path}:{number}: invalid JSONL") from error
        if not isinstance(value, dict):
            raise ValueError(f"{path}:{number}: event is not an object")
        events.append(value)
    return events


def shader_capture_summary(
    events: list[dict[str, object]], require_zero_misses: bool
) -> dict[str, int] | None:
    summaries = [
        event
        for event in events
        if event.get("event") == "native_renderer.shader_capture.summary"
    ]
    if not summaries:
        return None
    if len(summaries) != 1:
        raise RuntimeError("shader capture did not produce exactly one summary")
    summary = {
        name: int(summaries[0][name])
        for name in ("entries", "bytes", "duplicate_callbacks", "rejected_callbacks")
    }
    if require_zero_misses and summary["entries"]:
        raise RuntimeError(
            f"FH1 shader pack has {summary['entries']} runtime translation misses"
        )
    return summary


def load_corpus_summary(path: Path) -> dict[str, object]:
    if not path.is_file():
        raise RuntimeError(f"FH1 execution corpus was not written: {path}")
    corpus = json.loads(path.read_text(encoding="utf-8"))
    if corpus.get("schema") != "pinyon-shift.fh1-gpu-corpus.v3":
        raise RuntimeError(f"invalid FH1 execution corpus: {path}")
    if corpus.get("overflow") or corpus.get("collisions"):
        raise RuntimeError(f"incomplete FH1 execution corpus: {path}")
    return {
        "path": str(path),
        **{
            name: corpus[name]
            for name in (
                "unique_keys",
                "unique_passes",
                "overflow",
                "collisions",
                "pass_collisions",
            )
        },
    }


def run(args: argparse.Namespace) -> dict[str, object]:
    scenario = args.scenario.resolve()
    state_root = args.state_root.resolve()
    (
        captures,
        stop,
        required_native,
        image_limits,
        performance_limits,
        distinct_presentation_min,
        simulation_time_limits,
        capture_mae_minimums,
        race_hud_captures,
        race_hud_any_groups,
    ) = (
        parse_scenario(scenario)
    )
    require_image_reference(image_limits, args.baseline_dir, args.record_baseline)
    if args.require_zero_shader_misses and not (
        args.shader_pack and args.shader_capture_dir
    ):
        raise ValueError(
            "--require-zero-shader-misses requires --shader-pack and "
            "--shader-capture-dir"
        )
    if args.disc_shader_corpus_dir and not args.shader_capture_dir:
        raise ValueError(
            "--disc-shader-corpus-dir requires --shader-capture-dir"
        )
    disc_shader_corpus_dir = (
        resolve_disc_shader_corpus(args.disc_shader_corpus_dir)
        if args.disc_shader_corpus_dir
        else None
    )
    profiles = list((state_root / "user").glob("**/ForzaProfile/ForzaProfile"))
    if not profiles:
        raise ValueError(f"no FH1 profile below {state_root / 'user'}")
    output = (
        args.output.resolve()
        if args.output
        else (
            Path(".local/native-renderer/automated")
            / f"{scenario.stem}-{datetime.now(timezone.utc):%Y%m%dT%H%M%SZ}"
        ).resolve()
    )
    if output.exists():
        raise ValueError(f"refusing stale output directory: {output}")

    run_state_root = output.with_name(f"{output.name}.state")
    if run_state_root.exists():
        raise ValueError(f"refusing stale isolated state directory: {run_state_root}")
    prepare_isolated_state(state_root, run_state_root)
    seeded_shader_storage = (
        seed_fh1_shader_storage(state_root, run_state_root)
        if args.seed_shader_storage or args.shader_pack
        else []
    )
    seeded_pipeline_prewarm = (
        seed_fh1_pipeline_prewarm(state_root, run_state_root)
        if args.seed_pipeline_prewarm
        else None
    )
    staged_shader_pack = None
    if args.shader_pack:
        stage = subprocess.run(
            [
                sys.executable,
                str(Path(__file__).with_name("native-shader-pack.py")),
                "stage",
                str(args.shader_pack.resolve()),
                "--state-root",
                str(run_state_root),
            ],
            capture_output=True,
            text=True,
            check=True,
        )
        staged_shader_pack = Path(json.loads(stage.stdout)["destination"])

    logs = run_state_root / "logs"
    previous_logs = set(logs.glob("*.jsonl")) if logs.exists() else set()
    runtime_log = logs / "runtime.log"
    runtime_offset = runtime_log.stat().st_size if runtime_log.exists() else 0
    timeout = args.timeout or max(180, stop // 30 + 180)
    command = [
        "powershell.exe",
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(Path(__file__).with_name("launch-preview.ps1")),
        "-StateRoot",
        str(run_state_root),
        "-RenderTestScript",
        str(scenario),
        "-RenderTestOutput",
        str(output),
        "-RenderTestTimeoutSeconds",
        str(timeout),
    ]
    if args.configuration:
        command += ["-Configuration", args.configuration]
    if args.hidden:
        command.append("-Hidden")
    if args.collect_pass_inventory:
        command.append("-CollectFh1PassInventory")
    if args.shader_capture_dir:
        command.extend(
            ["-ShaderCaptureDir", str(args.shader_capture_dir.resolve())]
        )
    if disc_shader_corpus_dir:
        command.extend(
            ["-DiscShaderCorpusDir", str(disc_shader_corpus_dir)]
        )
    if args.include_opening_movies:
        command.append("-RenderTestIncludeOpeningMovies")
    if args.game_argument:
        command.extend(["-GameArgumentsJson", json.dumps(args.game_argument)])
    command.append("-Json")
    process = subprocess.run(
        command, capture_output=True, text=True, timeout=timeout + 30, check=False
    )
    if process.returncode:
        raise RuntimeError(
            f"FH1 render test failed ({process.returncode}):\n"
            f"{process.stdout}\n{process.stderr}"
        )
    new_logs = sorted(set(logs.glob("*.jsonl")) - previous_logs)
    if len(new_logs) != 1:
        raise RuntimeError(f"expected one new JSONL session, found {len(new_logs)}")
    event_log = new_logs[0]
    events = load_events(event_log)
    shader_capture = shader_capture_summary(
        events, args.require_zero_shader_misses
    )
    by_name: dict[str, list[dict[str, object]]] = {}
    for event in events:
        by_name.setdefault(str(event.get("event", "")), []).append(event)
    failures = [
        event
        for event in events
        if str(event.get("event", "")).endswith(".failure")
        or str(event.get("event", "")) in {"process.crash", "device.lost"}
    ]
    if failures:
        raise RuntimeError(f"diagnostic failure: {failures[0]}")
    configured = by_name.get("fh1.render_test.configured", [])
    completed = by_name.get("fh1.render_test.complete", [])
    captured_events = by_name.get("fh1.render_test.capture", [])
    if len(configured) != 1 or len(completed) != 1:
        raise RuntimeError("render test did not configure and complete exactly once")
    if completed[0].get("captures") != str(len(captures)):
        raise RuntimeError("render test completed with missing captures")
    if len(captured_events) != len(captures):
        raise RuntimeError("capture event count does not match the scenario")
    expected_captures = {(str(frame), name) for frame, name in captures}
    observed_captures = {
        (str(event.get("frame")), str(event.get("name")))
        for event in captured_events
    }
    if observed_captures != expected_captures:
        raise RuntimeError("capture events do not match requested frames and names")
    runtime_slice = ""
    if runtime_log.exists():
        with runtime_log.open("rb") as stream:
            stream.seek(runtime_offset)
            runtime_slice = stream.read().decode("utf-8", errors="replace")
    lowered = runtime_slice.lower()
    forbidden = (
        "device_removed",
        "device_hung",
        "device_reset",
        "device removed",
        "pipeline creation failed",
        "resource state warning",
        "tdr",
        "[critical] [gpu]",
        "[error] [gpu]",
    )
    hit = next((pattern for pattern in forbidden if pattern in lowered), None)
    if hit:
        raise RuntimeError(f"renderer log contains forbidden failure: {hit}")
    native_counts = {
        match.group("family"): int(match.group("count"))
        for match in NATIVE_COUNTER.finditer(runtime_slice)
    }
    missing_native = required_native - native_counts.keys()
    if missing_native:
        raise RuntimeError(
            "missing required native coverage: " + ", ".join(sorted(missing_native))
        )
    pass_families_by_id = {}
    for match in PASS_FAMILY.finditer(runtime_slice):
        family = {
            key: (
                value or None
                if key
                in {"family", "attachment", "first_family", "first_draw", "copy"}
                else int(value) if value is not None else None
            )
            for key, value in match.groupdict().items()
        }
        previous = pass_families_by_id.get(family["family"])
        if previous is None or family["samples"] >= previous["samples"]:
            pass_families_by_id[family["family"]] = family
    pass_families = sorted(
        pass_families_by_id.values(),
        key=lambda family: family["total_ns"],
        reverse=True,
    )
    corpus = None
    if args.collect_pass_inventory:
        session = str(events[0].get("session"))
        corpus = load_corpus_summary(
            run_state_root / "cache" / "fh1-gpu-corpus" / f"{session}.json"
        )

    image_results = []
    captured_by_key = {
        (str(event.get("frame")), str(event.get("name"))): event
        for event in captured_events
    }
    for frame, name in captures:
        summary = ppm_summary(output / f"{name}.ppm")
        summary["frame"] = frame
        event = captured_by_key[(str(frame), name)]
        if event.get("vehicle_pose_valid") == "1":
            summary["vehicle_pose"] = {
                axis: float(event[f"vehicle_{axis}"]) for axis in ("x", "y", "z")
            }
        image_results.append(summary)

    capture_mae = []
    for first, second, minimum in capture_mae_minimums:
        actual = compare_capture_mae(output, first, second)
        if actual < minimum:
            raise RuntimeError(
                f"captures {first} and {second} differ by only {actual:.3f} MAE "
                f"(minimum {minimum:.3f})"
            )
        capture_mae.append(
            {"first": first, "second": second, "minimum": minimum, "actual": actual}
        )

    race_hud = {
        name: race_hud_summary(output / f"{name}.ppm")
        for name in sorted(race_hud_captures)
    }
    for names in race_hud_any_groups:
        name, summary = first_race_hud_summary(output, names)
        race_hud[name] = summary

    perf_csv = event_log.with_suffix(".perf.csv")
    perf = subprocess.run(
        [
            sys.executable,
            str(Path(__file__).with_name("summarize-performance.py")),
            str(perf_csv),
            "--format",
            "json",
        ],
        capture_output=True,
        text=True,
        check=True,
    )
    performance = json.loads(perf.stdout)
    if performance_limits:
        median_max, present_min, simulation_min, simulation_max = performance_limits
        median = performance["frames"]["frame_time_us"]["median"]
        cadence = performance["presentation"]["cadence_hz"]
        present = cadence["present"]
        simulation = cadence["simulation_tick"]
        if median > median_max:
            raise RuntimeError(f"median frame time {median} us exceeds {median_max}")
        if present < present_min:
            raise RuntimeError(f"present cadence {present} Hz is below {present_min}")
        if not simulation_min <= simulation <= simulation_max:
            raise RuntimeError(
                f"simulation cadence {simulation} Hz is outside "
                f"{simulation_min}..{simulation_max}"
            )
    if distinct_presentation_min is not None:
        presentation = performance.get("presentation")
        if not presentation:
            raise RuntimeError("missing presentation telemetry")
        counters = presentation["counters"]
        distinct_count = max(
            0, counters["present_count"] - counters["duplicate_present_count"]
        )
        duration = performance["frames"]["measured_duration_seconds"]
        distinct_hz = distinct_count / duration
        if distinct_hz < distinct_presentation_min:
            raise RuntimeError(
                f"distinct presentation cadence {distinct_hz:.3f} Hz is below "
                f"{distinct_presentation_min}"
            )
    if simulation_time_limits is not None:
        simulation_time = performance.get("presentation", {}).get(
            "simulation_time"
        )
        if not simulation_time:
            raise RuntimeError("missing title simulation-time telemetry")
        minimum_ratio, maximum_ratio, maximum_invalid = simulation_time_limits
        ratio = simulation_time["wall_time_ratio"]
        invalid = simulation_time["invalid_deltas"]
        if not minimum_ratio <= ratio <= maximum_ratio:
            raise RuntimeError(
                f"title simulation-time ratio {ratio} is outside "
                f"{minimum_ratio}..{maximum_ratio}"
            )
        if invalid > maximum_invalid:
            raise RuntimeError(
                f"invalid title simulation deltas {invalid} exceed "
                f"{maximum_invalid}"
            )

    comparisons = []
    if args.baseline_dir:
        baseline_dir = args.baseline_dir.resolve()
        compare = Path(__file__).with_name("compare-native-renderer-images.py")
        comparison_names = image_limits or {
            name: DEFAULT_IMAGE_LIMITS for _, name in captures
        }
        for name, limits in comparison_names.items():
            comparison_path = output / f"{name}.comparison.json"
            comparison = subprocess.run(
                [
                    sys.executable,
                    str(compare),
                    str(output / f"{name}.ppm"),
                    str(baseline_dir / f"{name}.ppm"),
                    "--output",
                    str(comparison_path),
                    "--mean-absolute-error-max",
                    str(limits[0]),
                    "--root-mean-square-error-max",
                    str(limits[1]),
                    "--different-pixel-ratio-max",
                    str(limits[2]),
                    "--coverage-iou-min",
                    "0",
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            if comparison.returncode not in (0, 2):
                raise RuntimeError(
                    f"image comparison failed for {name}: {comparison.stderr}"
                )
            report = json.loads(comparison_path.read_text(encoding="utf-8"))
            comparisons.append(report)
            if comparison.returncode == 2:
                failed = [key for key, passed in report["checks"].items() if not passed]
                raise RuntimeError(
                    f"image regression for {name}: {', '.join(failed)}"
                )
    pose_comparisons = []
    if args.pose_baseline_result:
        baseline_result = json.loads(
            args.pose_baseline_result.read_text(encoding="utf-8")
        )
        pose_comparisons = compare_vehicle_poses(
            image_results,
            baseline_result.get("captures", []),
            args.pose_distance_max,
        )
    result = {
        "schema": SCHEMA,
        "result": "pass",
        "scenario": str(scenario),
        "state_root": str(run_state_root),
        "session": events[0].get("session"),
        "event_log": str(event_log),
        "performance_log": str(perf_csv),
        "captures": image_results,
        "native_counts": native_counts,
        "fh1_pass_families": pass_families,
        "fh1_execution_corpus": corpus,
        "pass_inventory_enabled": args.collect_pass_inventory,
        "shader_capture_dir": (
            str(args.shader_capture_dir.resolve()) if args.shader_capture_dir else None
        ),
        "disc_shader_corpus_dir": (
            str(disc_shader_corpus_dir) if disc_shader_corpus_dir else None
        ),
        "shader_pack": str(staged_shader_pack) if staged_shader_pack else None,
        "shader_capture": shader_capture,
        "seeded_shader_storage": seeded_shader_storage,
        "seeded_pipeline_prewarm": seeded_pipeline_prewarm,
        "opening_movies_included": args.include_opening_movies,
        "performance": performance,
        "comparisons": comparisons,
        "vehicle_pose_comparisons": pose_comparisons,
        "capture_mae": capture_mae,
        "race_hud": race_hud,
        "automation": (
            "fh1_wall_time_script"
            if "# clock-hz " in scenario.read_text(encoding="utf-8")
            else "fh1_guest_output_frame_script"
        ),
        "human_input": False,
        "computer_use": False,
    }
    (output / "result.json").write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf-8"
    )
    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run a deterministic FH1 renderer scenario."
    )
    parser.add_argument("scenario", type=Path)
    parser.add_argument("--state-root", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--baseline-dir", type=Path)
    parser.add_argument("--record-baseline", action="store_true")
    parser.add_argument("--pose-baseline-result", type=Path)
    parser.add_argument("--pose-distance-max", type=float, default=1.25)
    parser.add_argument("--collect-pass-inventory", action="store_true")
    parser.add_argument("--shader-capture-dir", type=Path)
    parser.add_argument("--disc-shader-corpus-dir", type=Path)
    parser.add_argument("--shader-pack", type=Path)
    parser.add_argument("--seed-shader-storage", action="store_true")
    parser.add_argument("--seed-pipeline-prewarm", action="store_true")
    parser.add_argument("--require-zero-shader-misses", action="store_true")
    parser.add_argument("--include-opening-movies", action="store_true")
    parser.add_argument("--timeout", type=int)
    parser.add_argument(
        "--configuration", choices=("Release", "RelWithDebInfo"),
        help="preview build to launch (launch-preview.ps1 default: Release)",
    )
    parser.add_argument(
        "--hidden", action="store_true",
        help="hide the window; frame pacing then matches routes recorded hidden",
    )
    parser.add_argument("--game-argument", action="append", default=[])
    args = parser.parse_args()
    try:
        result = run(args)
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
