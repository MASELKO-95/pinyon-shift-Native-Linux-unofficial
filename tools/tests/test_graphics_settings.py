import json
import pathlib
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/set-graphics-experiment.ps1"
QUALIFY_ZPD = ROOT / "tools/qualify-zpd.ps1"
POWERSHELL = shutil.which("powershell")


@unittest.skipUnless(POWERSHELL, "Windows PowerShell is required")
class GraphicsSettingsTests(unittest.TestCase):
    def test_zpd_qualification_plan_covers_required_matrix(self):
        completed = subprocess.run(
            [POWERSHELL, "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-File", str(QUALIFY_ZPD), "-Action", "Plan", "-Json"],
            capture_output=True, text=True,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        plan = json.loads(completed.stdout)
        self.assertEqual(len(plan["matrix"]), 6)
        self.assertEqual(plan["admission"]["required_cold_boots"], 10)
        self.assertEqual(plan["matrix"][2]["label"], "fast-layout")

    def run_tool(self, state, *arguments):
        completed = subprocess.run(
            [POWERSHELL, "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass",
             "-File", str(TOOL), "-StateRoot", str(state), *arguments, "-Json"],
            capture_output=True, text=True,
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        return json.loads(completed.stdout)

    def test_apply_migrates_schema_and_restore_recovers_previous_file(self):
        with tempfile.TemporaryDirectory(prefix="pinyon-settings-") as temporary:
            state = pathlib.Path(temporary)
            config = state / "config/pinyon_shift.toml"
            config.parent.mkdir(parents=True)
            original = "pinyon_shift_config_schema = 1\nmnk_mode = true\ncustom_value = 77\n"
            config.write_text(original, encoding="utf-8")
            result = self.run_tool(
                state, "-Action", "Apply", "-Anisotropy", "16",
                "-PostEffect", "fxaa", "-ResolutionScale", "2",
                "-OcclusionQuery", "fast",
                "-ZpdEndPolicy", "pairwise_sentinel",
                "-ZpdEndFallback", "none",
                "-PresentationFps", "30",
                "-RenderFps", "120",
                "-Preset", "experimental_2x",
                "-DisableMotionBlur", "true",
                "-DisableDepthOfField", "true",
            )
            updated = config.read_text(encoding="utf-8")
            self.assertEqual(result["settings"]["anisotropy"], 16)
            self.assertIn("pinyon_shift_config_schema = 23", updated)
            self.assertIn('fh1_renderer = "native"', updated)
            self.assertEqual(result["settings"]["renderer"], "native")
            self.assertIn("xma_relaxed_padding_admission = false", updated)
            self.assertEqual(result["settings"]["occlusion_query"], "fast")
            self.assertIn('occlusion_query = "fast"', updated)
            self.assertEqual(result["settings"]["zpd_end_policy"], "pairwise_sentinel")
            self.assertEqual(result["settings"]["zpd_end_fallback"], "none")
            self.assertEqual(result["settings"]["host_present_fps_limit"], 30)
            self.assertEqual(result["settings"]["fh1_render_fps_limit"], 120)
            self.assertIn("pinyon_shift_fh1_render_fps_limit = 120", updated)
            self.assertTrue(result["settings"]["host_present_sleep_spin"])
            self.assertTrue(result["settings"]["disable_motion_blur"])
            self.assertTrue(result["settings"]["disable_depth_of_field"])
            self.assertIn("custom_value = 77", updated)
            self.assertIn("draw_resolution_scale_x = 2", updated)
            self.assertEqual(result["settings"]["preset"], "experimental_2x")
            self.assertTrue(pathlib.Path(result["backup_path"]).is_file())
            self.run_tool(state, "-Action", "Restore")
            self.assertEqual(config.read_text(encoding="utf-8"), original)

    def test_reset_writes_supported_defaults_and_preserves_backup(self):
        with tempfile.TemporaryDirectory(prefix="pinyon-settings-") as temporary:
            state = pathlib.Path(temporary)
            config = state / "config/pinyon_shift.toml"
            config.parent.mkdir(parents=True)
            config.write_text("pinyon_shift_config_schema = 5\nswap_post_effect = \"fxaa\"\n", encoding="utf-8")
            result = self.run_tool(state, "-Action", "Reset")
            text = config.read_text(encoding="utf-8")
            self.assertIn("swap_post_effect = \"none\"", text)
            self.assertIn("disable_motion_blur = false", text)
            self.assertIn("disable_depth_of_field = false", text)
            self.assertIn("draw_resolution_scale_x = 1", text)
            self.assertIn("xma_relaxed_padding_admission = false", text)
            self.assertIn('occlusion_query = "legacy"', text)
            self.assertIn('zpd_end_policy = "report_layout"', text)
            self.assertIn('zpd_end_fallback = "pairwise_sentinel"', text)
            self.assertNotIn('readback_resolve', text)
            self.assertIn('clear_memory_page_state = true', text)
            self.assertIn('host_present_fps_limit = 0', text)
            self.assertIn('host_present_sleep_spin = true', text)
            self.assertIn('pinyon_shift_fh1_render_fps_limit = 0', text)
            self.assertIn('pinyon_shift_fh1_source_presentation = true', text)
            self.assertEqual(result["settings"]["preset"], "shipping_1x")
            self.assertTrue(pathlib.Path(result["backup_path"]).is_file())

    def test_apply_replaces_legacy_guest_vblank_with_render_limit(self):
        with tempfile.TemporaryDirectory(prefix="pinyon-settings-") as temporary:
            state = pathlib.Path(temporary)
            config = state / "config/pinyon_shift.toml"
            config.parent.mkdir(parents=True)
            config.write_text(
                "pinyon_shift_config_schema = 15\n"
                "pinyon_shift_fh1_guest_vblank_hz = 240\n"
                "pinyon_shift_native_renderer_texture_bridge = true\n"
                'pinyon_shift_native_renderer = "diagnostic_triangle"\n'
                "pinyon_shift_native_renderer_sky_horizon_suppression = true\n"
                "pinyon_shift_fh1_native_v4 = false\n",
                encoding="utf-8",
            )
            self.run_tool(state, "-Action", "Apply")
            text = config.read_text(encoding="utf-8")
            self.assertNotIn("pinyon_shift_fh1_guest_vblank_hz", text)
            self.assertNotIn("pinyon_shift_native_renderer_texture_bridge", text)
            self.assertNotIn("pinyon_shift_native_renderer =", text)
            self.assertNotIn("pinyon_shift_native_renderer_sky_horizon_suppression", text)
            self.assertNotIn("pinyon_shift_fh1_native_v4", text)
            self.assertIn("pinyon_shift_fh1_render_fps_limit = 0", text)

    def test_apply_selects_xenos_rollback_and_reset_restores_native(self):
        with tempfile.TemporaryDirectory(prefix="pinyon-settings-") as temporary:
            state = pathlib.Path(temporary)
            config = state / "config/pinyon_shift.toml"
            result = self.run_tool(state, "-Action", "Apply", "-Renderer", "xenos")
            self.assertEqual(result["settings"]["renderer"], "xenos")
            self.assertIn('fh1_renderer = "xenos"', config.read_text(encoding="utf-8"))
            result = self.run_tool(state, "-Action", "Reset")
            self.assertEqual(result["settings"]["renderer"], "native")
            self.assertIn('fh1_renderer = "native"', config.read_text(encoding="utf-8"))

    def test_experimental_3x_writes_4k_class_scale(self):
        with tempfile.TemporaryDirectory(prefix="pinyon-settings-") as temporary:
            state = pathlib.Path(temporary)
            result = self.run_tool(
                state, "-Action", "Apply", "-Preset", "experimental_3x"
            )
            text = (state / "config/pinyon_shift.toml").read_text(encoding="utf-8")
            self.assertEqual(result["settings"]["preset"], "experimental_3x")
            self.assertEqual(result["settings"]["resolution_scale"], 3)
            self.assertIn("draw_resolution_scale_x = 3", text)

    def test_apply_removes_retired_resolve_settings(self):
        with tempfile.TemporaryDirectory(prefix="pinyon-settings-") as temporary:
            state = pathlib.Path(temporary)
            config = state / "config/pinyon_shift.toml"
            config.parent.mkdir(parents=True)
            config.write_text(
                "pinyon_shift_config_schema = 19\n"
                "readback_resolve = \"full\"\n"
                "readback_resolve_half_pixel_offset = true\n",
                encoding="utf-8",
            )
            result = self.run_tool(state, "-Action", "Apply")
            text = config.read_text(encoding="utf-8")
            self.assertEqual(result["settings"]["preset"], "shipping_1x")
            self.assertNotIn("readback_resolve", text)
            self.assertTrue(pathlib.Path(result["backup_path"]).is_file())


if __name__ == "__main__":
    unittest.main()
