import json
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class NativeRendererContractTests(unittest.TestCase):








    def test_graphics_hook_has_one_pass_through_owner(self):
        analysis = (ROOT / "config/rexglue/analysis/main-xex.toml").read_text(
            encoding="utf-8"
        )
        self.assertEqual(analysis.count('name = "PinyonShiftObserveGraphicsFrame"'), 1)
        hook = analysis.split('name = "PinyonShiftObserveGraphicsFrame"', 1)[0]
        hook = hook.rsplit("[[midasm_hook]]", 1)[1]
        self.assertIn("address = 0x829EFEB8", hook)
        self.assertNotIn("jump_address", hook)
        self.assertNotIn("after_instruction", hook)
        self.assertNotIn("registers", hook)









    def test_dependency_ledger_keeps_gate_b_closed(self):
        ledger = (
            ROOT / "docs/native-renderer/GUEST_VISIBLE_RENDER_DEPENDENCIES.md"
        ).read_text(encoding="utf-8")
        self.assertIn("No render target is suppression-eligible yet.", ledger)
        self.assertIn("Gate B remains closed", ledger)
        self.assertIn("unknown_uninstrumented", ledger)
        self.assertIn("unknown_unclassified", ledger)
        self.assertIn("capture-native-renderer-census.ps1", ledger)
        self.assertIn("summarize-native-renderer-census.py", ledger)





    def test_census_has_no_native_renderer_or_suppression_api(self):
        source = (ROOT / "src/native_renderer/graphics_hooks.cpp").read_text(
            encoding="utf-8"
        )
        forbidden = (
            "native_rhi",
            "NativeRHI",
            "IssueDraw",
            "IssueCopy",
            "SetDrawSuppression",
            "SetCopySuppression",
        )
        for token in forbidden:
            self.assertNotIn(token, source)

        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertEqual(cmake.count("src/native_renderer/graphics_hooks.cpp"), 2)









    def test_suppression_admission_is_fail_closed_and_non_mutating(self):
        evaluator = (
            ROOT / "tools/evaluate-native-renderer-suppression.py"
        ).read_text(encoding="utf-8")
        self.assertIn('"guest_cpu_visibility"', evaluator)
        self.assertIn('"later_gpu_consumers"', evaluator)
        self.assertIn('"rollback_switch"', evaluator)
        self.assertIn('"suppression_allowed": False', evaluator)
        self.assertIn('"draw_suppression_implemented": False', evaluator)
        self.assertIn('"resolve_suppression_implemented": False', evaluator)
        self.assertNotIn("SetDrawSuppression", evaluator)


    def test_consumer_family_classifier_is_exact_and_fail_closed(self):
        classifier = json.loads(
            (ROOT / "config/native-renderer/consumer-family-classifier.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(
            "pinyon-shift.native-renderer-consumer-family-classifier.v1",
            classifier["schema"],
        )
        self.assertEqual(38, len(classifier["rules"]))
        self.assertEqual(
            38, len({rule["shader_family_id"] for rule in classifier["rules"]})
        )
        self.assertTrue(
            all(
                rule["semantic_role"] == "retained_unknown"
                for rule in classifier["rules"]
            )
        )
        self.assertTrue(
            all(rule["native_coverage"] is False for rule in classifier["rules"])
        )


    def test_complete_pass_export_spans_anchor_and_follower(self):
        exporter = (
            ROOT / "tools/export-native-renderer-renderdoc.py"
        ).read_text(encoding="utf-8")
        wrapper = (
            ROOT / "tools/export-native-renderer-renderdoc.ps1"
        ).read_text(encoding="utf-8")
        self.assertIn("PASS_NATIVE_ANCHOR_MARKER", exporter)
        self.assertIn("PASS_XENOS_ANCHOR_MARKER", exporter)
        self.assertIn("PASS_NATIVE_FOLLOWER_MARKER", exporter)
        self.assertIn("PASS_XENOS_FOLLOWER_MARKER", exporter)
        self.assertIn("_export_pass_span", exporter)
        self.assertIn('"draw_count": 2', exporter)
        self.assertIn('"suppression_allowed": False', exporter)
        self.assertIn("[switch]$CompletePass", wrapper)
        self.assertIn("PINYON_SHIFT_RENDERDOC_COMPLETE_PASS", wrapper)
        self.assertNotIn("SetDrawSuppression", exporter)










    def test_census_ledger_tracks_exact_starting_baseline(self):
        ledger = (ROOT / "docs/native-renderer/archive/RENDER_PASS_CENSUS.md").read_text(
            encoding="utf-8"
        )
        self.assertIn("cafc7233fef9e039f163d11023f40eccb22e8fc1", ledger)
        self.assertIn("f5337cdc947ff6d4c4196737e2c807a48f2a1fc2", ledger)
        self.assertIn("Unknown work stays on Xenos.", ledger)


if __name__ == "__main__":
    unittest.main()
