import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "summarize_fh1_stencil_census",
    ROOT / "tools/summarize-fh1-stencil-census.py",
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def depth_control(stencil=True, fail=0, zpass=0, zfail=0, backface=False,
                  fail_bf=0, zpass_bf=0, zfail_bf=0):
    value = (int(stencil) | 0b110 | (int(backface) << 7) | (fail << 11) | (zpass << 14)
             | (zfail << 17) | (fail_bf << 23) | (zpass_bf << 26) | (zfail_bf << 29))
    return f"{value:08X}"


def state(surface, draws, control, ref_mask="00FF0000", ref_mask_bf=None):
    result = {"surface": surface, "draws": draws, "depth_control": control,
              "stencil_ref_mask": ref_mask}
    if ref_mask_bf is not None:
        result["stencil_ref_mask_bf"] = ref_mask_bf
    return result


def window(first, states):
    return {
        "first_frame": first,
        "last_frame": first + 59,
        "surfaces": [
            {"id": 0, "surface_info": "00020500", "depth_info": "00010000", "bound": 3},
            {"id": 1, "surface_info": "00000500", "depth_info": "000002D0", "bound": 1},
            {"id": 2, "surface_info": "00000500", "depth_info": "00000000", "bound": 2},
        ],
        "draw_states": states,
    }


class StencilCensusTests(unittest.TestCase):
    def test_matches_the_executor_nonzero_write_rule(self):
        classify = MODULE.classify
        self.assertFalse(classify(state(0, 1, depth_control(stencil=False, zpass=2),
                                        "00FF0001"))["enabled"])
        # KEEP and ZERO never leave a nonzero value.
        self.assertFalse(classify(state(0, 1, depth_control(zpass=1)))["writes"])
        # REPLACE writes only a nonzero reference under the write mask.
        self.assertFalse(classify(state(0, 1, depth_control(zpass=2), "00FF0000"))["writes"])
        self.assertFalse(classify(state(0, 1, depth_control(zpass=2), "00F00001"))["writes"])
        self.assertTrue(classify(state(0, 1, depth_control(zpass=2), "00FF0001"))["writes"])
        # Any other op writes unless the write mask is zero.
        self.assertTrue(classify(state(0, 1, depth_control(zfail=3)))["writes"])
        self.assertFalse(classify(state(0, 1, depth_control(zfail=3), "0000FF00"))["writes"])

    def test_back_face_needs_its_own_mask(self):
        control = depth_control(backface=True, zpass_bf=6)
        self.assertIsNone(MODULE.classify(state(0, 1, control))["writes"])
        self.assertTrue(MODULE.classify(state(0, 1, control, ref_mask_bf="00FF0000"))["writes"])
        self.assertFalse(MODULE.classify(state(0, 1, control, ref_mask_bf="00000000"))["writes"])

    def test_tabulates_per_depth_surface_and_window(self):
        records = [
            window(1, [state(0, 10, depth_control(stencil=False)),
                       state(0, 4, depth_control(zpass=2), "00FF0080"),
                       state(1, 7, depth_control(zpass=1)),
                       state(2, 99, depth_control(zpass=2), "00FF0080")]),
            window(61, [state(0, 5, depth_control(stencil=False)),
                        state(1, 2, depth_control(backface=True, zpass_bf=6))]),
        ]
        summary = MODULE.summarize(records)
        rows = {row["depth_surface"]: row for row in summary["surfaces"]}
        self.assertEqual(summary["windows"], 2)
        # Color-only surfaces have no depth surface.
        self.assertEqual(set(rows), {"D24FS8@0/4x/pitch1280", "D24S8@720/1x/pitch1280"})
        scene = rows["D24FS8@0/4x/pitch1280"]
        self.assertEqual((scene["draws"], scene["stencil_draws"], scene["writing_draws"]),
                         (19, 4, 4))
        self.assertEqual((scene["writing_windows"], scene["windows"]), (1, 2))
        self.assertEqual(scene["writing_states"][0]["draws"], 4)
        shadow = rows["D24S8@720/1x/pitch1280"]
        self.assertEqual((shadow["writing_draws"], shadow["unknown_draws"]), (0, 2))
        self.assertEqual((shadow["writing_windows"], shadow["windows"]), (1, 2))
        self.assertIn("| D24FS8@0/4x/pitch1280 | 19 | 4 | 4 | 0 | 1 / 2 |",
                      MODULE.markdown(summary, 8))

    def test_frame_range_selects_windows(self):
        records = [window(1, [state(0, 1, depth_control(stencil=False))]),
                   window(61, [state(0, 1, depth_control(stencil=False))])]
        self.assertEqual(MODULE.summarize(records, first_frame=70)["windows"], 1)
        self.assertEqual(MODULE.summarize(records, last_frame=30)["windows"], 1)


if __name__ == "__main__":
    unittest.main()
