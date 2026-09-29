import csv
import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/map-generated-lines.py"


def load_module():
    spec = importlib.util.spec_from_file_location("map_generated_lines", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


MAP = load_module()

GENERATED = """#include "pinyon_shift_funcs.1.h"

DEFINE_REX_FUNC(sub_82000000) {
\tREX_FUNC_PROLOGUE();
\tswitch (rex_dispatch_address) {
\t\tcase 0x82000008: goto loc_82000008;
\t}

\t// mflr r12
\tctx.r12.u64 = ctx.lr;
\t// li r3,1
\tctx.r3.s64 = 1;
loc_82000008:
\t// addi r3,r3,1
\tctx.r3.s64 = ctx.r3.s64 + 1;
\t// blr
\treturn;
}

DEFINE_REX_FUNC(sub_82000100) {
\t// nop
\tctx.r0.u64 = 0;
}
"""


class MapGeneratedLinesTests(unittest.TestCase):
    def write_capture(self, root: pathlib.Path, fingerprint: str) -> None:
        generated = root / "generated"
        generated.mkdir()
        (generated / "pinyon_shift_recomp.1.cpp").write_text(GENERATED, encoding="utf-8")
        (generated / "codegen.stamp").write_text(json.dumps({"fingerprint": "abc"}), encoding="utf-8")
        capture = root / "capture"
        capture.mkdir()
        (capture / "capture.json").write_text(
            json.dumps({"codegen_fingerprint": fingerprint}), encoding="utf-8")
        with (capture / "samples.csv").open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(["timestamp_ms", "cpu_ms", "source_file", "source_line"])
            # Code of `li r3,1`, of `addi` after the label, of the second
            # function, a sample outside generated code and one above any
            # instruction.
            writer.writerow(["1", "1", "D:/gen/pinyon_shift_recomp.1.cpp", "12"])
            writer.writerow(["2", "1", "D:\\gen\\pinyon_shift_recomp.1.cpp", "15"])
            writer.writerow(["3", "1", "pinyon_shift_recomp.1.cpp", "22"])
            writer.writerow(["4", "1", "command_processor.cpp", "12"])
            writer.writerow(["5", "1", "pinyon_shift_recomp.1.cpp", "4"])

    def test_samples_map_to_the_instruction_their_line_belongs_to(self):
        with tempfile.TemporaryDirectory(prefix="pinyon-map-lines-") as temporary:
            root = pathlib.Path(temporary)
            self.write_capture(root, "abc")
            self.assertEqual(
                MAP.main([str(root / "capture"), "--generated-dir", str(root / "generated")]), 0)
            with (root / "capture/samples.csv").open(encoding="utf-8", newline="") as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(
                [(row["guest_function"], row["guest_address"]) for row in rows],
                [("sub_82000000", "82000004"), ("sub_82000000", "82000008"),
                 ("sub_82000100", "82000100"), ("", ""), ("", "")])

    def test_a_different_codegen_fingerprint_is_refused(self):
        with tempfile.TemporaryDirectory(prefix="pinyon-map-lines-") as temporary:
            root = pathlib.Path(temporary)
            self.write_capture(root, "older")
            self.assertEqual(
                MAP.main([str(root / "capture"), "--generated-dir", str(root / "generated")]), 2)
            self.assertEqual(
                MAP.main([str(root / "capture"), "--generated-dir", str(root / "generated"),
                          "--force"]), 0)


if __name__ == "__main__":
    unittest.main()
