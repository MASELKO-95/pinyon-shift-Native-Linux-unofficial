import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class NativeRendererPhaseCProfileTests(unittest.TestCase):
    def test_profile_arms_compatible_phase_c_gates(self):
        capture = (ROOT / "tools/capture-native-renderer-census.ps1").read_text(
            encoding="utf-8"
        )
        self.assertIn("[switch]$PhaseCQualification", capture)
        block = capture.split("if ($PhaseCQualification) {", 1)[1].split(
            "if (-not $StateRoot)", 1
        )[0]
        self.assertIn("$Scene = 'open_world_day'", block)
        self.assertIn("$ContinuousWorldWorkset = $true", block)
        self.assertIn("$ContinuousTrackWorld = $true", block)
        self.assertIn("$ContinuousStaticWorld = $true", block)
        self.assertIn("$VehicleDrawCorrelation = $true", block)
        self.assertNotIn("$ShadowDepthBatch = $true", block)
        self.assertNotIn("$VehicleResourceContribution", block)


if __name__ == "__main__":
    unittest.main()
