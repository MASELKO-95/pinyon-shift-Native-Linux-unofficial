#!/usr/bin/env python3
"""Summarize default-off FH1 texture reload probe messages."""

import argparse
import collections
import json
from pathlib import Path


MARKERS = {
    "attempts": "FH1 texture reload attempt ",
    "invalidations": "FH1 texture invalidated ",
    "ranges": "FH1 texture invalidation range ",
    "publications": "FH1 resolve publication ",
    "consumers": "FH1 texture reload consumer ",
}


def read_events(path):
    events = {name: [] for name in MARKERS}
    for line in path.open(errors="ignore"):
        for name, marker in MARKERS.items():
            offset = line.find(marker)
            if offset >= 0:
                events[name].append(json.loads(line[offset + len(marker) :]))
                break
    return events


def summarize(events):
    ranked = collections.defaultdict(lambda: {"loads": 0, "bytes": 0})
    for event in events["attempts"]:
        key = (
            event["base"], event["mips"], event["width"], event["height"],
            event["depth"], event["format"], event["scaled"],
        )
        item = ranked[key]
        item["loads"] += 1
        item["bytes"] += (
            event["base_bytes"] if event["base_dirty"] else 0
        ) + (event["mips_bytes"] if event["mips_dirty"] else 0)
    top = []
    for key, values in sorted(
        ranked.items(), key=lambda item: (item[1]["bytes"], item[1]["loads"]), reverse=True
    ):
        top.append(dict(zip(
            ("base", "mips", "width", "height", "depth", "format", "scaled"), key
        )) | values)
    invalidations = collections.Counter(
        (event["gpu"], event["part"]) for event in events["invalidations"]
    )
    publications = collections.Counter(
        (event["address"], event["length"], event["dest_info"], event["dest_pitch"])
        for event in events["publications"]
    )
    consumers = collections.Counter(
        (event["base"], event["width"], event["height"], event["format"],
         event["vs"], event["ps"])
        for event in events["consumers"]
    )
    return {
        "reload_attempts": len(events["attempts"]),
        "requested_bytes": sum(item["bytes"] for item in ranked.values()),
        "invalidations": {
            f'{"gpu" if gpu else "cpu"}_{part}': count
            for (gpu, part), count in sorted(invalidations.items())
        },
        "invalidation_ranges": {
            "gpu": sum(event["gpu"] for event in events["ranges"]),
            "cpu": sum(not event["gpu"] for event in events["ranges"]),
        },
        "resolve_publications": [
            {"address": key[0], "length": key[1], "dest_info": key[2],
             "dest_pitch": key[3], "count": count}
            for key, count in publications.most_common()
        ],
        "reload_consumers": [
            {"base": key[0], "width": key[1], "height": key[2],
             "format": key[3], "vs": key[4], "ps": key[5], "count": count}
            for key, count in consumers.most_common()
        ],
        "textures": top,
    }


def self_test():
    result = summarize({
        "attempts": [
            {"base": "1000", "mips": "0", "width": 4, "height": 4, "depth": 1,
             "format": 7, "scaled": 0, "base_dirty": True, "mips_dirty": False,
             "base_bytes": 64, "mips_bytes": 0},
            {"base": "1000", "mips": "0", "width": 4, "height": 4, "depth": 1,
             "format": 7, "scaled": 0, "base_dirty": True, "mips_dirty": False,
             "base_bytes": 64, "mips_bytes": 0},
        ],
        "invalidations": [{"gpu": True, "part": "base"}],
        "ranges": [{"gpu": False}, {"gpu": True}],
        "publications": [{"address": "1000", "length": 64,
                          "dest_info": 7, "dest_pitch": 4}],
        "consumers": [{"base": "1000", "width": 4, "height": 4,
                       "format": 7, "vs": "A", "ps": "B"}],
    })
    assert result["reload_attempts"] == 2
    assert result["requested_bytes"] == 128
    assert result["textures"][0]["loads"] == 2
    assert result["invalidations"] == {"gpu_base": 1}
    assert result["resolve_publications"][0]["count"] == 1
    assert result["reload_consumers"][0]["ps"] == "B"
    print("PASS")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.log:
        parser.error("log is required unless --self-test is used")
    print(json.dumps(summarize(read_events(args.log)), indent=2))


if __name__ == "__main__":
    main()
