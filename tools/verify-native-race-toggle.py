"""Check in-race native/compatibility switching on captured guest output."""

import sys
from pathlib import Path


output = Path(sys.argv[1])
header = b"P6\n1280 720\n255\n"
sky = bytes((28, 56, 110))


def sky_count(name: str) -> int:
    image = (output / f"{name}.ppm").read_bytes()
    assert image.startswith(header) and len(image) == len(header) + 1280 * 720 * 3
    return image.count(sky)


expected = {
    "before-on": False,
    "native-on": True,
    "before-off": True,
    "compatibility-off": False,
    "before-on-again": False,
    "native-on-again": True,
}
if "--cold-start" in sys.argv[2:]:
    expected = {"compatibility-off": False}
for name, native in expected.items():
    count = sky_count(name)
    assert (count > 1000) == native, f"wrong output mode at {name}: {count}"
if "--cold-start" in sys.argv[2:]:
    for names, minimum in (
        (("warmup-on", "warmup-on-later", "native-on", "before-off"), 2),
        (("warmup-on-again", "native-on-again"), 1),
    ):
        counts = [sky_count(name) for name in names]
        assert sum(count > 1000 for count in counts) >= minimum, (
            f"native output did not recover during {names}: {counts}"
        )
print("native race output: live on/off/on switching passed")
