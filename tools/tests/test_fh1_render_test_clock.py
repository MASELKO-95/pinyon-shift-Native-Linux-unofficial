import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'clock_check', Path(__file__).parents[1] / 'check-fh1-render-test-clock.py')
clock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(clock)


class RenderTestClockTests(unittest.TestCase):
    def test_clock_bounds_and_missed_input_are_independent_gates(self):
        events = [dict(event='fh1.render_test.configured', clock='wall_time',
                       clock_hz='60', input_steps='2', captures='2')]
        events += [dict(event='fh1.render_test.input_step', index=str(i),
                        skipped_steps='0', scheduled_frame=str(i), observed_frame=str(i))
                   for i in range(2)]
        events += [dict(event='fh1.render_test.capture', name=str(i), frame='1',
                        trigger_output_frame=str(i), trigger_elapsed_us=str(100_000*i - 20_000),
                        capture_begin_elapsed_us=str(100_000*i - 19_000),
                        capture_end_elapsed_us=str(100_000*i - 18_000)) for i in (1, 3)]
        for event in events:
            event['session'] = 'test'
        durations = [0, 100_000, 100_000, 100_000, 100_000]
        result = clock.check_clock(events, durations)
        self.assertTrue(result['passed'])
        self.assertEqual([20_000, 118_002], result['route_origin_after_csv_origin_us'])
        # A timing-valid capture does not excuse an omitted press.
        result = clock.check_clock([events[0], *events[2:]], durations)
        self.assertFalse(result['passed'])
        self.assertIn('not every scheduled input', result['failures'][0])
        # Reject an incompatible anchor instead of assuming a shared origin.
        events[-1]['trigger_elapsed_us'] = '100000'
        self.assertFalse(clock.check_clock(events, durations)['passed'])
