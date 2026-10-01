import pathlib
import unittest


REPOSITORY_ROOT = pathlib.Path(__file__).resolve().parents[2]
SCANNER = REPOSITORY_ROOT / "src" / "native_renderer" / "graphics_hooks.cpp"


class NativeRendererReadbackParityContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scanner = SCANNER.read_text(encoding="utf-8")







if __name__ == "__main__":
    unittest.main()
