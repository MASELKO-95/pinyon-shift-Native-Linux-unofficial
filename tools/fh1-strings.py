"""List the keys and text of a Forza Horizon string table (.str).

Mods rename UI text with the mod API's set_ui_string(table, key, text), which
needs the table's name and the 16-bit key of the string. This prints them:

  fh1-strings.py --archive <game>/media/StringTables/EN.zip --table PauseMenu.str
  fh1-strings.py --file PauseMenu.str --grep resume

A table starts with a 24-byte LSB2 header whose u32 at +12 is the offset of
the string chunk: [u32 size][u32 count], count + 1 entries of (u16 key, u32
character offset into the pool) in key order closed by a 0xFFFF sentinel,
then the pool of NUL-terminated UTF-16BE strings. Archive entries are
LZX-compressed and need the native extractor
(pinyon_shift_fh1_archive_extract, built with the host).
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import struct
import sys
from pathlib import Path
from zipfile import ZipFile

MAGIC = b"LSB2"
SENTINEL = 0xFFFF
DEFAULT_EXTRACTOR = Path("out/build/win-amd64-release/pinyon_shift_fh1_archive_extract.exe")


class StringTableError(ValueError):
    pass


def parse(data: bytes) -> list[tuple[int, str]]:
    if len(data) < 24 or data[:4] != MAGIC:
        raise StringTableError("not an LSB2 string table")
    chunk = struct.unpack_from(">I", data, 12)[0]
    if chunk + 8 > len(data):
        raise StringTableError("string chunk is outside the file")
    size, count = struct.unpack_from(">II", data, chunk)
    end = chunk + size
    entries_at = chunk + 8
    pool = entries_at + 6 * (count + 1)
    if end > len(data) or pool > end:
        raise StringTableError("string chunk is truncated")
    entries = [struct.unpack_from(">HI", data, entries_at + 6 * i) for i in range(count + 1)]
    if entries[-1][0] != SENTINEL:
        raise StringTableError("missing the 0xFFFF sentinel")
    strings = []
    for key, offset in entries[:-1]:
        at = pool + 2 * offset
        characters = []
        while True:
            if at + 2 > end:
                raise StringTableError(f"string 0x{key:04X} runs past the chunk")
            code = struct.unpack_from(">H", data, at)[0]
            if code == 0:
                break
            characters.append(code)
            at += 2
        text = struct.pack(f">{len(characters)}H", *characters).decode("utf-16-be", "replace")
        strings.append((key, text))
    return strings


def read_archive_entry(archive: Path, table: str, extractor: Path) -> bytes:
    spec = importlib.util.spec_from_file_location(
        "inspect_fh1_ui", Path(__file__).with_name("inspect-fh1-ui.py"))
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    assert spec.loader
    spec.loader.exec_module(module)
    with ZipFile(archive) as zipped:
        matches = [info for info in zipped.infolist() if info.filename.lower() == table.lower()]
    if not matches:
        raise StringTableError(f"{table} is not in {archive}")
    return module.extract_entry(archive, matches[0], extractor, "auto", 1 << 24)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--file", type=Path, help="an extracted .str file")
    source.add_argument("--archive", type=Path, help="a StringTables/<language>.zip")
    parser.add_argument("--table", help="the table inside --archive, such as PauseMenu.str")
    parser.add_argument("--extractor", type=Path, default=DEFAULT_EXTRACTOR)
    parser.add_argument("--grep", help="only strings containing this text (any case)")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    try:
        if args.file:
            data = args.file.read_bytes()
        else:
            if not args.table:
                parser.error("--archive needs --table")
            data = read_archive_entry(args.archive, args.table, args.extractor)
        strings = parse(data)
    except (OSError, StringTableError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    if args.grep:
        strings = [(key, text) for key, text in strings if args.grep.lower() in text.lower()]
    if args.json:
        print(json.dumps([{"key": f"0x{key:04X}", "text": text} for key, text in strings],
                         ensure_ascii=False, indent=2))
    else:
        for key, text in strings:
            print(f"0x{key:04X}  {text}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
