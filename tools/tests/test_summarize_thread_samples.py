import importlib.util
import pathlib
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools/summarize-thread-samples.py"


def load_module():
    spec = importlib.util.spec_from_file_location("summarize_thread_samples", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


SUMMARY = load_module()


def write_capture(directory: pathlib.Path) -> None:
    (directory / "threads.csv").write_text(
        "thread,name\n7,GPU Commands (F8000018)\n", encoding="utf-8"
    )
    (directory / "stacks.csv").write_text(
        "stack_id,stack\n"
        '0,"app!Main;app!Draw;app!Bind"\n'
        '1,"app!Main;app!Draw"\n'
        '2,"app!Main;app!Wait;ntdll!NtDelayExecution"\n',
        encoding="utf-8",
    )
    # Four samples in the window (one of them waiting) and one outside it.
    (directory / "samples.csv").write_text(
        "t_ms,thread,cycles,stack_id\n"
        "100.0,7,1000,0\n"
        "200.0,7,1000,0\n"
        "300.0,7,2000,1\n"
        "400.0,7,0,2\n"
        "5000.0,7,9000,1\n",
        encoding="utf-8",
    )


class SummarizeThreadSamplesTest(unittest.TestCase):
    def summarize(self, weight):
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            write_capture(directory)
            threads, stacks, samples = SUMMARY.load(directory)
            return SUMMARY.summarize(threads, stacks, samples, 0.0, 1.0, 10, weight=weight)

    def test_wall_time_shares_and_waiting(self):
        thread = self.summarize("samples")["threads"]["GPU Commands (F8000018)"]
        self.assertEqual(thread["samples"], 4)
        self.assertEqual(thread["waiting_percent"], 25.0)
        inclusive = {entry["name"]: entry["percent"] for entry in thread["inclusive"]}
        self.assertEqual(inclusive["app!Main"], 100.0)
        self.assertEqual(inclusive["app!Draw"], 75.0)
        exclusive = {entry["name"]: entry["percent"] for entry in thread["exclusive"]}
        self.assertEqual(exclusive["app!Bind"], 50.0)
        self.assertEqual(exclusive["ntdll!NtDelayExecution"], 25.0)
        modules = {entry["name"]: entry["percent"] for entry in thread["modules"]}
        self.assertEqual(modules, {"app": 75.0, "ntdll": 25.0})

    def test_cycle_weights_ignore_blocked_samples(self):
        thread = self.summarize("cycles")["threads"]["GPU Commands (F8000018)"]
        self.assertEqual(thread["waiting_percent"], 0.0)
        exclusive = {entry["name"]: entry["percent"] for entry in thread["exclusive"]}
        self.assertEqual(exclusive["app!Bind"], 50.0)
        self.assertEqual(exclusive["app!Draw"], 50.0)

    def test_focus_keeps_matching_stacks(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            write_capture(directory)
            threads, stacks, samples = SUMMARY.load(directory)
            report = SUMMARY.summarize(threads, stacks, samples, 0.0, 1.0, 10, focus="Bind")
        self.assertEqual(report["threads"]["GPU Commands (F8000018)"]["samples"], 2)


    def test_leaf_lines_merge_by_function(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = pathlib.Path(temp)
            write_capture(directory)
            (directory / "stacks.csv").write_text(
                "stack_id,stack\n"
                '0,"app!Main;app!Bind @bind.cpp:3"\n'
                '1,"app!Main;app!Bind @bind.cpp:4"\n'
                '2,"app!Main;ntdll!NtDelayExecution"\n',
                encoding="utf-8",
            )
            threads, stacks, samples = SUMMARY.load(directory)
            report = SUMMARY.summarize(threads, stacks, samples, 0.0, 1.0, 10)
        thread = report["threads"]["GPU Commands (F8000018)"]
        inclusive = {entry["name"]: entry["percent"] for entry in thread["inclusive"]}
        self.assertEqual(inclusive["app!Bind"], 75.0)
        exclusive = {entry["name"]: entry["percent"] for entry in thread["exclusive"]}
        self.assertEqual(exclusive["app!Bind"], 75.0)
        lines = {entry["name"]: entry["percent"] for entry in thread["exclusive_lines"]}
        self.assertEqual(lines["app!Bind @bind.cpp:3"], 50.0)
        self.assertEqual(lines["app!Bind @bind.cpp:4"], 25.0)


if __name__ == "__main__":
    unittest.main()
