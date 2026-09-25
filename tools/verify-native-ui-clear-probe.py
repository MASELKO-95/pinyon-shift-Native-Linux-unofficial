"""Verify that the opt-in pre-UI clear retains the race HUD and mode fallback."""

import sys
from pathlib import Path


HEADER = b"P6\n1280 720\n255\n"
BLUE = bytes((23, 57, 117))  # Presented RGB for the probe's R10 clear.


def pixels(path: Path) -> bytes:
    data = path.read_bytes()
    assert data.startswith(HEADER) and len(data) == len(HEADER) + 1280 * 720 * 3, path
    return data[len(HEADER):]


def count_color(image: bytes, color: bytes, bounds=(0, 0, 1280, 720)) -> int:
    left, top, right, bottom = bounds
    return sum(image[(y * 1280 + x) * 3:(y * 1280 + x) * 3 + 3] == color
               for y in range(top, bottom) for x in range(left, right))


if __name__ == "__main__":
    output = Path(sys.argv[1])
    race = pixels(output / "race-sustained.ppm")
    assert count_color(race, BLUE) > 700_000, "pre-UI clear missing from race"
    for name, bounds in {
        "laps": (60, 20, 260, 170),
        "place": (1000, 20, 1230, 230),
        "minimap": (70, 410, 260, 565),
        "speed": (1010, 500, 1220, 680),
    }.items():
        width = (bounds[2] - bounds[0]) * (bounds[3] - bounds[1])
        assert width - count_color(race, BLUE, bounds) > 100, f"missing {name} HUD"
    for name in ("race-paused", "free-roam-after-retire", "title-settled"):
        image = pixels(output / f"{name}.ppm")
        assert count_color(image, BLUE) < 1000, f"clear leaked into {name}"
        assert sum(image) // len(image) > 20, f"blank {name}"
    print("pre-UI clear: race HUD retained; pause, free roam and title compatible")
