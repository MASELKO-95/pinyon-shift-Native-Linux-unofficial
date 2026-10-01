"""The SDK reads CRT semantic-hook addresses at the analysis root only."""
from pathlib import Path
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NonlocalJumpConfigTests(unittest.TestCase):
    def test_crt_addresses_are_root_scalars(self):
        config = tomllib.loads(
            (ROOT / 'config/rexglue/analysis/main-xex.toml').read_text())
        self.assertEqual(config.get('setjmp_address'), 0x82A81E80)
        self.assertEqual(config.get('longjmp_address'), 0x82A81950)
        for hook in config['midasm_hook']:
            self.assertNotIn('setjmp_address', hook)
            self.assertNotIn('longjmp_address', hook)


if __name__ == '__main__':
    unittest.main()
