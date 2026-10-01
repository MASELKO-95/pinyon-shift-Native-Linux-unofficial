import importlib.util
import json
import pathlib
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "build_fh1_gpu_prewarm", ROOT / "tools/build-fh1-gpu-prewarm.py"
)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class BuildFh1GpuPrewarmTests(unittest.TestCase):
    def test_emits_deduplicated_fh1_pipeline_draw_and_copy_keys(self):
        corpus = {
            "schema": "pinyon-shift.fh1-gpu-corpus.v3",
            "key_version": 2,
            "entries": [
                {"kind": 1, "identity": "0000000000000002", "pipeline_state": "000000000000000A"},
                {"kind": 1, "identity": "0000000000000001", "pipeline_state": "000000000000000A"},
                {"kind": 2, "identity": "0000000000000003", "pipeline_state": "0000000000000000"},
            ],
        }
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "corpus.json"
            output = root / "prewarm.txt"
            source.write_text(json.dumps(corpus), encoding="utf-8")

            self.assertEqual(MODULE.build(source, output), (1, 2, 1))
            self.assertEqual(
                output.read_text(encoding="ascii").splitlines(),
                [
                    MODULE.SCHEMA,
                    "P 000000000000000A",
                    "D 0000000000000001",
                    "D 0000000000000002",
                    "C 0000000000000003",
                ],
            )

    def test_merges_an_existing_manifest_with_new_corpus(self):
        corpus = {
            "schema": "pinyon-shift.fh1-gpu-corpus.v3",
            "key_version": 2,
            "entries": [
                {"kind": 1, "identity": "0000000000000002", "pipeline_state": "000000000000000B"},
            ],
        }
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "corpus.json"
            previous = root / "previous.txt"
            output = root / "prewarm.txt"
            source.write_text(json.dumps(corpus), encoding="utf-8")
            previous.write_text(
                MODULE.SCHEMA + "\nP 000000000000000A\nD 0000000000000001\n",
                encoding="ascii",
            )

            self.assertEqual(MODULE.build([source, previous], output), (2, 2, 0))
            self.assertEqual(
                output.read_text(encoding="ascii").splitlines(),
                [
                    MODULE.SCHEMA,
                    "P 000000000000000A",
                    "P 000000000000000B",
                    "D 0000000000000001",
                    "D 0000000000000002",
                ],
            )

    def test_stages_read_only_fh1_native_catalog(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            legacy = root / "legacy"
            shareable = legacy / "shaders/shareable"
            shareable.mkdir(parents=True)
            analysis = b"FHSA" + (1).to_bytes(4, "little") + (1).to_bytes(4, "little") + bytes(4)
            (legacy / "fh1-native-shaders-v2.bin").write_bytes(analysis)
            first = (2).to_bytes(8, "little") + bytes(64)
            duplicate = first
            ignored = (3).to_bytes(8, "little") + bytes(64)
            (shareable / "4D5309C9.rtv.d3d12.xpso").write_bytes(
                b"XEPS" + bytes(8) + first + duplicate + ignored
            )
            manifest = root / "fh1-gpu-prewarm-v3.txt"
            manifest.write_text(MODULE.SCHEMA + "\nP 0000000000000002\n", encoding="ascii")

            MODULE.stage_native_catalog(legacy, manifest, root)

            self.assertEqual((root / "fh1-native-shaders-v2.bin").read_bytes(), analysis)
            self.assertEqual(
                (root / "fh1-native-pipelines-v1.bin").read_bytes(),
                b"XEPS" + bytes(8) + first,
            )

    def test_all_stored_pipelines_prewarm_the_producer_route(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            legacy = root / "legacy"
            shareable = legacy / "shaders/shareable"
            shareable.mkdir(parents=True)
            records = [value.to_bytes(8, "little") + bytes(64)
                       for value in (5, 4, 0x6E456C111D3FA84D)]
            (shareable / "4D5309C9.rtv.d3d12.xpso").write_bytes(b"XEPS" + bytes(8) + b"".join(records))
            corpus = root / "corpus.json"
            corpus.write_text(json.dumps({
                "schema": "pinyon-shift.fh1-gpu-corpus.v3", "key_version": 2, "entries": []}))
            output = root / "manifest.txt"
            extra = MODULE.stored_pipelines(legacy)
            self.assertEqual(MODULE.build(corpus, output, extra), (2, 0, 0))
            self.assertEqual(output.read_text().splitlines()[1:],
                             ["P 0000000000000004", "P 0000000000000005"])

    def test_owned_native_pipeline_keeps_draw_without_guest_descriptor(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            corpus = root / "corpus.json"
            corpus.write_text(json.dumps({
                "schema": "pinyon-shift.fh1-gpu-corpus.v3", "key_version": 2,
                "entries": [{"kind": 1, "identity": "0000000000000001",
                             "pipeline_state": "6E456C111D3FA84D"}],
            }))
            output = root / "manifest.txt"
            self.assertEqual(MODULE.build(corpus, output), (0, 1, 0))
            self.assertIn("D 0000000000000001", output.read_text())

    def test_fh1_selection_does_not_mutate_persistent_cache_validation(self):
        source = (
            ROOT
            / "thirdparty/shiftglue-sdk/src/graphics/d3d12/pipeline_cache.cpp"
        ).read_text(encoding="utf-8")

        self.assertIn("fh1_pipeline_prewarm_descriptions", source)
        self.assertNotIn("std::erase_if(pipeline_stored_descriptions", source)
        self.assertIn(
            "sizeof(PipelineStoredDescription) * pipeline_stored_descriptions.size()",
            source,
        )
        self.assertIn(
            "StartCreationThreads(creation_thread_needed_count)", source
        )
        self.assertIn(
            "blocking && creation_threads_busy_ != 0", source
        )
        self.assertIn('cache_root / "fh1-native-shaders-v2.bin"', source)
        self.assertIn('cache_root / "fh1-native-pipelines-v1.bin"', source)
        self.assertIn('OpenFile(shader_storage_file_path, "rb")', source)

    def test_runtime_does_not_compile_shader_analysis_sources(self):
        source = (
            ROOT / "thirdparty/shiftglue-sdk/src/graphics/CMakeLists.txt"
        ).read_text(encoding="utf-8")
        runtime_sources = source.split("set(REXGPU_XENOS_SOURCES", 1)[1].split(")", 1)[0]

        self.assertNotIn("pipeline/shader/translator.cpp", runtime_sources)
        self.assertNotIn("pipeline/shader/translator_disasm.cpp", runtime_sources)
        self.assertIn("${REXGPU_FH1_SHADER_ANALYSIS_SOURCES}", source)

if __name__ == "__main__":
    unittest.main()
