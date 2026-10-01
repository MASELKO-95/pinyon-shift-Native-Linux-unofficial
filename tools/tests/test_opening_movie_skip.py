import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def function_body(source: str, signature: str) -> str:
    return source.split(signature, 1)[1].split("\n}\n", 1)[0]


class OpeningMovieSkipTests(unittest.TestCase):
    def test_skip_is_limited_to_splash_intros(self):
        source = (ROOT / "src/pinyon_shift_runtime_hooks.cpp").read_text(
            encoding="utf-8"
        )
        skip = function_body(source, "void PinyonShiftCompleteOpeningMovie(")
        self.assertIn("g_opening_movie_is_splash.load", skip)
        observer = function_body(source, "void PinyonShiftObserveGuestFileOpen(")
        self.assertIn('"splash_intros"', observer)
        self.assertIn('ends_with(".wmv")', observer)

    def test_observer_sees_every_kernel_file_open(self):
        # Title movie opens reach NtCreateFile through function pointers, so
        # the observer must be registered with the kernel, not guest hooks.
        app = (ROOT / "src/pinyon_shift_app.cpp").read_text(encoding="utf-8")
        self.assertIn(
            "SetGuestFileOpenObserver(&PinyonShiftObserveGuestFileOpen)", app
        )
        kernel = (
            ROOT / "thirdparty/shiftglue-sdk/src/kernel/xboxkrnl/xboxkrnl_io.cpp"
        ).read_text(encoding="utf-8")
        create = function_body(kernel, "u32 NtCreateFile_entry(")
        self.assertIn("guest_file_open_observer.load", create)


if __name__ == "__main__":
    unittest.main()
