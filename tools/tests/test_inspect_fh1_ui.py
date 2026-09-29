import importlib.util
import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "inspect_fh1_ui", ROOT / "tools" / "inspect-fh1-ui.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(MODULE)


class InspectFh1UiTests(unittest.TestCase):
    def test_resolves_local_header_and_payload_offsets(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "ui.zip"
            with ZipFile(archive, "w") as zipped:
                zipped.writestr("Scenes/ui4/925_PAUSE_MENU.bgf", b"payload")
            with ZipFile(archive) as zipped:
                info = zipped.getinfo("Scenes/ui4/925_PAUSE_MENU.bgf")
            resolved = MODULE.payload_offset(archive, info.header_offset, "auto")
            self.assertEqual(b"payload", MODULE._read_range(archive, resolved, 7))
            self.assertEqual(resolved, MODULE.payload_offset(archive, resolved, "auto"))

    def test_rejects_truncated_payload_range(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "ui.zip"
            archive.write_bytes(b"short")
            with self.assertRaises(MODULE.UiInspectionError):
                MODULE._read_range(archive, 4, 2)

    def test_catalogs_entries_font_aliases_and_asset_references(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game = root / ".local" / "game"
            media = game / "media"
            media.mkdir(parents=True)
            archive = media / "UI.zip"
            fontmap = b'<fontmap><mapping fontname="Horizon_A" target="A" /></fontmap>'
            scene = b"AnarkBGF GAME:\\Media\\UI\\Textures\\UI4\\Common\\Tick.xds"
            with ZipFile(archive, "w") as zipped:
                zipped.writestr("fontmap.xml", fontmap)
                zipped.writestr("Scenes/ui4/925_PAUSE_MENU.bgf", scene)
            output = root / ".local" / "ui" / "catalog.json"
            result = MODULE.inspect(
                game,
                output,
                [archive],
                patterns=["Scenes/ui4/*.bgf"],
            )
            entries = result["archives"][0]["entries"]
            pause = next(item for item in entries if item["scene_family"] == "925_PAUSE_MENU")
            self.assertTrue(pause["extracted"])
            self.assertIn("GAME:\\Media\\UI\\Textures\\UI4\\Common\\Tick.xds", pause["asset_references"])
            self.assertEqual("A", result["fontmap"]["mappings"][0]["target"])
            self.assertEqual(zlib.crc32(scene) & 0xFFFFFFFF, int(pause["crc32"], 16))
            self.assertTrue(output.is_file())


def make_xds(width, height, format_number, endianness, payload, swizzle="XYZW",
             tiled=True, packed=False, pitch=32):
    selectors = sum("XYZW01".index(channel) << (3 * index) for index, channel in enumerate(swizzle))
    fetch = (
        2 | (3 << 2) | (3 << 4) | (3 << 6) | ((pitch // 32) << 22) | (int(tiled) << 31),
        format_number | (endianness << 6),
        (width - 1) | ((height - 1) << 13),
        selectors << 1,
        0,
        (1 << 9) | (int(packed) << 11),
    )
    header = struct.pack(">7I", 3, 1, 0, 0, 0, 0xFFFF0000, 0xFFFF0000) + struct.pack(">6I", *fetch)
    return header + payload


def make_vfont(glyphs):
    """Build a FontCompiler vfont with one triangle per glyph."""
    hash_size = 4
    data = bytearray(0x400)
    data[0:6] = b"vfont\x00"
    data[6:19] = b"12.07.06.0035"
    struct.pack_into(">I", data, 0x34C, len(glyphs))
    struct.pack_into(">II", data, 0x35C, 0x36C, hash_size)
    after = 0x36C + 4 * hash_size
    metrics = after + 8
    records = metrics + 0x4C
    struct.pack_into(">II", data, after, metrics, records)
    struct.pack_into(">III", data, metrics + 8, 3 * 8 * len(glyphs), 0, 3 * len(glyphs))
    struct.pack_into(">fff", data, metrics + 0x14, 0.25, 14.0, 1.0)
    struct.pack_into(">ii", data, metrics + 0x24, 1115, 372)
    data = data[:records]
    for index, (code, advance) in enumerate(glyphs):
        data += struct.pack(">IfIIIIIfff", code, advance, 0xFFFF0000, 24 * index, 3, 3 * index, 1,
                            -0.01, 0.0, 1.0)
    gpu = bytearray()
    for _ in glyphs:
        for x, y in ((0.0, 0.0), (0.5, 0.0), (0.0, 0.5)):
            gpu += struct.pack(">eeHH", x, y, 0, 0x3C00)
    for index in range(len(glyphs)):
        gpu += struct.pack(">3H", 0, 1, 2)
    caff = bytearray(0x190)
    caff[0:4] = b"CAFF"
    struct.pack_into(">I", caff, 0x44, len(data))
    struct.pack_into(">I", caff, 0x60, len(data))
    return bytes(caff + data + gpu)


class Fh1UiAssetDecodingTests(unittest.TestCase):
    def test_tiled_offsets_cover_each_tile_once(self):
        for log2 in (2, 3, 4):
            offsets = {MODULE.tiled_offset_2d(x, y, 32, log2) for y in range(32) for x in range(32)}
            self.assertEqual(offsets, set(range(0, 32 * 32 << log2, 1 << log2)))

    def test_swizzled_rgba_texture_round_trips_to_stored_bytes(self):
        payload = bytes((index * 7) & 0xFF for index in range(32 * 32 * 4))
        data = make_xds(20, 12, 50, 2, payload, swizzle="ZYXW")
        header, rgba = MODULE.decode_xds(data)
        self.assertEqual((20, 12, "ZYXW"), (header["width"], header["height"], header["swizzle"]))
        # Texel (0, 0) is the first stored dword, byte-swapped as 8-in-32.
        stored = payload[0:4][::-1]
        self.assertEqual(bytes((stored[2], stored[1], stored[0], stored[3])), rgba[0:4])
        png = MODULE.encode_png(20, 12, rgba)
        self.assertEqual((20, 12, rgba), MODULE.decode_png(png))
        self.assertEqual("bytes", MODULE.texture_round_trip(data, png))
        edited = bytearray(rgba)
        edited[0] ^= 0xFF
        changed = MODULE.reencode_xds(data, bytes(edited))
        self.assertEqual(bytes(edited), MODULE.decode_xds(changed)[1])

    def test_small_texture_base_level_sits_in_packed_mip_tail(self):
        block = struct.pack("<HHI", 0xF800, 0x001F, 0)  # every texel is color0 (red)
        swapped = bytes(block[index ^ 1] for index in range(8))
        payload = bytearray(32 * 32 * 8)
        offset = MODULE.tiled_offset_2d(0, 16 // 4, 32, 3)
        payload[offset:offset + 8] = swapped
        data = make_xds(12, 4, 51, 1, bytes(payload), packed=True, pitch=32)
        _, rgba = MODULE.decode_xds(data)
        self.assertEqual(bytes((255, 0, 0, 255)), rgba[0:4])

    def test_one_channel_formats_replicate_into_every_component(self):
        block = bytes([0x21, 0x43, 0x65, 0x87, 0xA9, 0xCB, 0xED, 0x0F])  # 4-bit values 1..15, 0
        swapped = bytes(block[index ^ 1] for index in range(8))
        payload = bytearray(32 * 32 * 8)
        payload[0:8] = swapped
        _, rgba = MODULE.decode_xds(make_xds(4, 4, 58, 1, bytes(payload), swizzle="111W", pitch=32))
        self.assertEqual(bytes((255, 255, 255, 17)), rgba[0:4])
        self.assertEqual(bytes((255, 255, 255, 0)), rgba[60:64])

    def test_png_reader_undoes_every_filter(self):
        width, rows = 2, [bytes([10, 20, 30, 40, 50, 60, 70, 80]), bytes([15, 25, 35, 45, 55, 65, 75, 85])]
        raw = b""
        for kind, row in zip((1, 4), rows):
            previous = rows[0] if kind == 4 else bytes(8)
            encoded = bytearray(row)
            for index in range(8):
                left = row[index - 4] if index >= 4 else 0
                up, corner = previous[index], previous[index - 4] if index >= 4 else 0
                if kind == 1:
                    encoded[index] = (row[index] - left) & 0xFF
                else:
                    estimate = left + up - corner
                    distances = (abs(estimate - left), abs(estimate - up), abs(estimate - corner))
                    encoded[index] = (row[index] - (left, up, corner)[distances.index(min(distances))]) & 0xFF
            raw += bytes([kind]) + bytes(encoded)
        png = MODULE.encode_png(width, 2, b"".join(rows))
        at = png.index(b"IDAT") - 4
        size = struct.unpack_from(">I", png, at)[0]
        body = zlib.compress(raw)
        png = (png[:at] + struct.pack(">I", len(body)) + b"IDAT" + body
               + struct.pack(">I", zlib.crc32(b"IDAT" + body) & 0xFFFFFFFF) + png[at + 12 + size:])
        self.assertEqual((2, 2, b"".join(rows)), MODULE.decode_png(png))

    def test_vector_font_metrics_and_coverage(self):
        summary = MODULE.parse_vfont(make_vfont([(0x41, 0.6), (0x42, 0.55), (0x44, 0.5)]))
        self.assertEqual(3, summary["glyph_count"])
        self.assertEqual((1115, 372, 14.0), (summary["ascent"], summary["descent"], summary["design_size"]))
        self.assertEqual(["U+0041-U+0042", "U+0044"], summary["code_point_ranges"])
        self.assertEqual({"code_point": 0x42, "advance": 0.55, "offset_x": -0.01, "vertices": 3,
                          "triangles": 1}, summary["glyphs"][1])
        with self.assertRaises(MODULE.UiInspectionError):
            MODULE.parse_vfont(make_vfont([(0x41, 0.6)])[:-8])

    def test_fontmap_keeps_fonts_and_overrides(self):
        fontmap = MODULE.parse_fontmap(
            b'<fontmap><mapping fontname="Horizon_A" target="A"/><font target="A" minsize="0"/>'
            b'<fallback_adjuster_sd lang="JP" font="E"><override min_size="18" scene="947_HUD"/>'
            b'</fallback_adjuster_sd></fontmap>'
        )
        self.assertEqual([{"target": "A", "minsize": "0"}], fontmap["fonts"])
        self.assertEqual([{"lang": "JP", "font": "E", "overrides": [{"min_size": "18", "scene": "947_HUD"}]}],
                         fontmap["fallback_adjusters_sd"])


    def test_catalogue_decodes_textures_and_fonts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            game = root / ".local" / "game"
            (game / "media" / "ui" / "fonts").mkdir(parents=True)
            (game / "media" / "ui" / "fonts" / "JPB.sbm").write_bytes(b"bitmap")
            archive = game / "media" / "ui" / "Textures.zip"
            texture = make_xds(8, 8, 50, 2, bytes(range(256)) * 16)
            with ZipFile(archive, "w") as zipped:
                zipped.writestr("Hud/Tick.xds", texture)
                zipped.writestr("a_vector_aa.dt", make_vfont([(0x41, 0.6)]))
                zipped.writestr("a_vector_aa.dt.log.xml", b"<Value name='face' value='Horizon_A' type='String'/>")
            result = MODULE.inspect(game, root / ".local" / "ui.json", [archive], decode_assets=True,
                                    png_directory=root / ".local" / "png")
            entries = {entry["name"]: entry for entry in result["archives"][0]["entries"]}
            tick = entries["Hud/Tick.xds"]["texture"]
            self.assertEqual("bytes", tick["round_trip"])
            self.assertTrue(Path(tick["png"]).is_file())
            self.assertEqual(1, entries["a_vector_aa.dt"]["vector_font"]["glyph_count"])
            self.assertEqual("Horizon_A", entries["a_vector_aa.dt.log.xml"]["font_compiler_face"])
            self.assertEqual({"bytes": 1}, result["texture_summary"]["round_trip"])
            self.assertEqual("JPB.sbm", result["loose_fonts"][0]["name"])
            with self.assertRaises(MODULE.UiInspectionError):
                MODULE.inspect(game, root / ".local" / "ui.json", [archive], decode_assets=True,
                               png_directory=root / "png")


if __name__ == "__main__":
    unittest.main()
