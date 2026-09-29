import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SDK = "thirdparty/shiftglue-sdk"
GRAPHICS_SOURCES = (
    *(f"{SDK}/include/rex/graphics/{name}.h" for name in (
        "fh1_shader_pack", "d3d12/host_render_config", "d3d12/pipeline_cache",
        "d3d12/primitive_processor", "d3d12/shader", "flags", "format/ucode",
        "primitive_processor", "registers", "util/draw", "xenos", "pipeline_util",
        "pipeline/render_target/psi_color_format", "pipeline/shader/dxbc_translator")),
    f"{SDK}/include/rex/graphics/register_table.inc",
    *(f"{SDK}/src/graphics/{name}.cpp" for name in (
        "fh1_shader_pack", "d3d12/host_render_config", "d3d12/pipeline_cache",
        "d3d12/primitive_processor", "d3d12/shader", "flags", "format/ucode",
        "primitive_processor", "registers", "util/draw", "xenos",
        "pipeline/shader/dxbc_translator", "pipeline/shader/spirv_translator")),
)


@unittest.skipUnless(shutil.which("powershell"), "Windows PowerShell required")
class ShaderPreparationTests(unittest.TestCase):
    def test_prepare_reuse_repair_invalidation_and_failed_retry(self):
        with tempfile.TemporaryDirectory(prefix="pinyon-prepare-") as directory:
            root = pathlib.Path(directory)
            for path in (
                "out/build/win-amd64-release/pinyon_shift.exe",
                "out/build/win-amd64-release/rexgpu-fh1.dll",
                "out/build/win-amd64-release/rexruntime.dll",
                "config/release-toolchain.json", "config/supported-dumps.json",
                "config/render-tests/fh1-shader-preparation.fh1test",
                "tools/extract-fh1-shader-corpus.py", "tools/build-fh1-gpu-prewarm.py",
                "tools/fh1_archive_extract.cpp",
                "tools/native-shader-pack.py",
                "src/native_renderer/shader_capture.cpp",
                *GRAPHICS_SOURCES,
            ):
                target = root / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text("input")
            for name in ("prepare-fh1-shaders.ps1", "release-common.ps1"):
                shutil.copyfile(ROOT / "tools" / name, root / "tools" / name)
            (root / "tools/produce-fh1-artifacts.ps1").write_text(r'''
param($WorkRoot, $RenderTestScript, $GameRoot, $BuildDirectory, $RuntimeConfig, $Scale, $SeedShaderCacheRoot, $ShaderMissDir, [switch]$Hidden, [switch]$JsonEvents, [switch]$IncludeOpeningMovies, [switch]$AllowPipelineDiscovery)
$root = Split-Path $PSScriptRoot -Parent
Add-Content (Join-Path $root 'calls.txt') $(if ($ShaderMissDir) { "$Scale+misses" } else { $Scale })
$work = Join-Path $root $WorkRoot
$cache = Join-Path $work 'strict-state/cache'
[void][IO.Directory]::CreateDirectory((Join-Path $cache 'shaders/shareable'))
[IO.File]::WriteAllText((Join-Path $cache 'fh1-native-shaders-v2.bin'), 'analysis')
if ($env:PINYON_TEST_FAIL -eq '1') { throw 'simulated producer interruption' }
foreach ($file in @('fh1-gpu-prewarm-v3.txt', 'fh1-native-pipelines-v1.bin', "shaders/shareable/test-$Scale.pnsp")) {
    [IO.File]::WriteAllText((Join-Path $cache $file), 'validated artifact')
}
'{"result":"shaders-validated"}' | Set-Content (Join-Path $work 'production.json')
''')
            state = root / "state"
            save = state / "user/ForzaProfile/ForzaProfile"
            save.parent.mkdir(parents=True)
            save.write_bytes(b"untouched save")
            environment = os.environ.copy()
            environment.update(PINYON_TEST_ROOT=str(root), PINYON_TEST_DRIVER="one")
            command = r'''
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Utility/Microsoft.PowerShell.Utility.psd1')
Import-Module (Join-Path $PSHOME 'Modules/Microsoft.PowerShell.Management/Microsoft.PowerShell.Management.psd1')
function Get-CimInstance { [pscustomobject]@{ PNPDeviceID = 'test GPU'; DriverVersion = $env:PINYON_TEST_DRIVER } }
function Get-Process { return $null }
& (Join-Path $env:PINYON_TEST_ROOT 'tools/prepare-fh1-shaders.ps1') -StateRoot (Join-Path $env:PINYON_TEST_ROOT 'state') -JsonEvents
'''

            def run(success=True):
                result = subprocess.run(["powershell", "-NoProfile", "-Command", command],
                                        env=environment, capture_output=True, text=True)
                self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
                self.assertEqual(save.read_bytes(), b"untouched save")
                return result

            def calls():
                return (root / "calls.txt").read_text().splitlines()

            active = state / "cache/fh1-artifacts.json"
            run()
            self.assertTrue(active.is_file())
            self.assertEqual(calls(), ["1"])
            run()
            # Creation of the default runtime config must not invalidate preparation.
            (state / "config").mkdir()
            config = state / "config/pinyon_shift.toml"
            config.write_text("draw_resolution_scale_x = 1\ndraw_resolution_scale_y = 1\n")
            run()
            self.assertEqual(calls(), ["1"])
            (state / "cache/fh1-native-shaders-v2.bin").write_text("damaged")
            run()
            self.assertEqual((state / "cache/fh1-native-shaders-v2.bin").read_text(), "analysis")
            self.assertEqual(calls(), ["1"])
            previous = active.read_bytes()
            environment.update(PINYON_TEST_DRIVER="two", PINYON_TEST_FAIL="1")
            self.assertIn("simulated producer interruption", run(False).stderr)
            self.assertEqual(active.read_bytes(), previous)
            environment["PINYON_TEST_FAIL"] = "0"
            run()
            self.assertEqual(calls(), ["1", "1", "1"])
            config.write_text("draw_resolution_scale_x = 2\ndraw_resolution_scale_y = 2\n")
            run()
            self.assertEqual(calls()[-1], "2")
            receipt = json.loads(active.read_text())
            self.assertEqual(len(receipt["files"]), 4)
            # A forged traversal is rejected and replaced with the validated set.
            receipt["files"][0]["path"] = "../../user/ForzaProfile/ForzaProfile"
            active.write_text(json.dumps(receipt))
            run()
            self.assertEqual(calls(), ["1", "1", "1", "2"])
            legacy = state / "cache/shaders/shareable"
            legacy.mkdir(parents=True, exist_ok=True)
            (legacy / "4D5309C9.xsh").write_bytes(b"shader")
            (legacy / "4D5309C9.rtv.d3d12.xpso").write_bytes(b"pipeline")
            run()
            run()
            self.assertEqual(calls(), ["1", "1", "1", "2", "2"])
            (legacy / "4D5309C9.xsh").write_bytes(b"more shaders")
            run()
            self.assertEqual(calls(), ["1", "1", "1", "2", "2", "2"])
            # Rebuilt binaries and sources outside the graphics inputs keep the
            # prepared graphics; a translator change prepares them again.
            for binary in ("pinyon_shift.exe", "rexgpu-fh1.dll", "rexruntime.dll"):
                (root / "out/build/win-amd64-release" / binary).write_text("rebuilt")
            (root / SDK / "src/graphics/pipeline/shader/spirv_translator.cpp").write_text("edited")
            run()
            self.assertEqual(len(calls()), 6)
            (root / SDK / "src/graphics/pipeline/shader/dxbc_translator.cpp").write_text("edited")
            run()
            run()
            self.assertEqual(len(calls()), 7)
            # A pack miss the game recorded prepares the pack again with it.
            misses = state / "cache/fh1-shader-misses"
            misses.mkdir()
            (misses / "vertex-AFF858C659830DD3-0000000000000001.bin").write_bytes(b"ucode")
            run()
            run()
            self.assertEqual(calls()[-1], "2+misses")
            self.assertEqual(len(calls()), 8)
            # Preparing every scale stages the other scales' packs too, once,
            # and keeps the chosen scale's set active (NP-4.7).
            config.write_text("draw_resolution_scale_x = 2\ndraw_resolution_scale_y = 2\n"
                              "pinyon_shift_prepare_all_scales = true\n")
            run()
            self.assertEqual(calls()[8:], ["1+misses", "3+misses", "4+misses"])
            for scale in (1, 2, 3, 4):
                self.assertTrue((state / f"cache/shaders/shareable/test-{scale}.pnsp").is_file())
            self.assertIn("test-2.pnsp", active.read_text())
            run()
            self.assertEqual(len(calls()), 11)



class ShaderPreparationKeyInputTests(unittest.TestCase):
    def test_every_hashed_repository_file_exists(self):
        # A deleted key input would make every real preparation fail.
        script = (ROOT / "tools/prepare-fh1-shaders.ps1").read_text(encoding="utf-8")
        start = script.index("'config/release-toolchain.json'")
        end = script.index("$legacyShaderCache")
        section = script[start:end]
        names = re.findall(r"'((?:config|tools|src)/[^'$]+)'", section)
        names += [f"{SDK}/{name}" for name in re.findall(r'"\$sdk/([^"$]+)"', section)]
        sdk_block = section[section.index("'fh1_shader_pack'"):
                            section.index("$graphicsSources.AddRange")]
        for name in re.findall(r"'([a-z0-9_/]+)'", sdk_block):
            names += [f"{SDK}/include/rex/graphics/{name}.h", f"{SDK}/src/graphics/{name}.cpp"]
        missing = [name for name in names if not (ROOT / name).exists()]
        self.assertGreater(len(names), 30)
        self.assertEqual(missing, [])


if __name__ == "__main__":
    unittest.main()
