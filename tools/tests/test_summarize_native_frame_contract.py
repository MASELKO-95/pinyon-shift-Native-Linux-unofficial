import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "summarize_native_frame_contract",
    ROOT / "tools/summarize-native-frame-contract.py",
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def window(first, draws, **overrides):
    record = {
        "first_frame": first,
        "last_frame": first + 59,
        "draws": draws,
        "surfaces": [{
            "id": 0,
            "surface_info": "00020500",  # pitch 1280, 4x
            "depth_info": "00010000",  # D24FS8 at tile 0
            "color_info": ["00032A00", "00000000", "00000000", "00000000"],
            "window_offset": "00000000",
            "bound": 3,
            "host_formats": ["00000001", "00000003", "0", "0", "0"],
            "draws": draws,
        }],
        "draw_states": [{
            "vs": "A", "ps": "B", "guest_primitive": 6, "draws": draws,
            "memexport": False, "occlusion_query": False,
        }],
        "textures": [{
            "format": 18, "dimension": 1, "width": 256, "height": 256, "depth": 1,
            "tiled": 1, "packed_mips": 1, "mip_min": 0, "mip_max": 8, "signs": 0,
            "fetches": 10, "from_resolve": 0,
        }],
        "copies": [{
            "copy_control": "00000060",  # color0, average 4 samples
            "dest_info": "003E0380",  # 2_10_10_10, bias -2
            "dest_pitch": "02D00500",
            "surface_info": "00020500",
            "source_info": "00032A00",
            "depth_info": "00010000",
            "succeeded": True,
            "copies": 2,
            "bytes": 7372800,
        }],
        "optimized_clears": {"2": 1},
        "swaps": [{"format": 54, "width": 1280, "height": 720, "swaps": 60}],
        "zpd_events": 0,
        "cost_ns": 30_000_000,
        "overflow": {"surfaces": 0, "draw_states": 0, "textures": 0, "copies": 0},
    }
    record.update(overrides)
    return record


class SummarizeNativeFrameContractTests(unittest.TestCase):
    def test_modes_group_windows_and_decode_registers(self):
        records = [window(1, 600), window(61, 1200, zpd_events=12, cost_ns=60_000_000)]
        summary = MODULE.summarize(records, MODULE.parse_modes(["menu:0-60"]))

        self.assertEqual(set(summary), {"menu", "unlabelled"})
        menu = summary["menu"]
        self.assertEqual(menu["frames"], 60)
        self.assertEqual(menu["draws_per_frame"], 10.0)
        self.assertEqual(menu["census_ms_per_frame"], 0.5)
        surface = menu["surfaces"][0]
        self.assertEqual((surface["pitch"], surface["msaa"]), (1280, "4x"))
        self.assertEqual(surface["colors"][0]["format"], "2_10_10_10_FLOAT")
        self.assertEqual(surface["depth"]["format"], "D24FS8")
        copy = menu["copies"][0]
        self.assertEqual(copy["dest_format"], "2_10_10_10")
        self.assertEqual(copy["dest_exp_bias"], -2)
        self.assertEqual(menu["textures"][0]["format"], "DXT1")
        self.assertEqual(menu["swaps"][0]["format"], "2_10_10_10_AS_16_16_16_16")
        self.assertEqual(summary["unlabelled"]["zpd_events_per_frame"], 0.2)

    def test_markdown_lists_every_mode(self):
        summary = MODULE.summarize([window(1, 60)], [])
        text = MODULE.markdown(summary)
        self.assertIn("## unlabelled", text)
        self.assertIn("| 1280 | 4x |", text)


if __name__ == "__main__":
    unittest.main()
