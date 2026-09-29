"""Bounded manual-play recorder. Runs beside the game; never changes game inputs."""
import argparse
import csv
import ctypes as ct
from ctypes import wintypes as wt
import datetime as dt
import json
import math
import os
from pathlib import Path
import statistics
import sys
import time


def utc():
    return dt.datetime.now(dt.timezone.utc).isoformat()


class IoCounters(ct.Structure):
    _fields_ = [(name, ct.c_uint64) for name in (
        'read_operations', 'write_operations', 'other_operations',
        'read_bytes', 'write_bytes', 'other_bytes')]


def process_io(kernel, process):
    kernel.GetProcessIoCounters.argtypes = [wt.HANDLE, ct.POINTER(IoCounters)]
    counters = IoCounters()
    if not kernel.GetProcessIoCounters(process, ct.byref(counters)):
        return None
    return {name: getattr(counters, name) for name, _ in counters._fields_}


def atomic_json(path, value):
    staging = path.with_suffix('.tmp')
    staging.write_text(json.dumps(value, indent=2), encoding='utf-8')
    staging.replace(path)


def runtime_logs(root):
    result = []
    for path in root.glob('runtime*.log'):
        try:
            result.append((path, path.stat()))
        except FileNotFoundError:
            pass
    return sorted(result, key=lambda item: item[1].st_mtime_ns)


def find_session_csv(logs, pid, existing):
    matches = [path for path in logs.glob(f'*-p{pid}.perf.csv') if path.name not in existing]
    if len(matches) > 1:
        raise RuntimeError('Ambiguous new performance session for process')
    return matches[0] if matches else None


class Tail:
    """Keep incomplete lines until the producer flushes them."""
    def __init__(self):
        self.offset = 0
        self.pending = b''

    def read(self, path, limit=1024 * 1024, identity=None):
        with path.open('rb') as stream:
            stat = os.fstat(stream.fileno())
            if identity is not None and (stat.st_dev, stat.st_ino) != identity:
                return []  # Rotation raced the directory listing; retry next poll.
            stream.seek(self.offset)
            chunk = stream.read(limit)
        self.offset += len(chunk)
        lines = (self.pending + chunk).split(b'\n')
        self.pending = lines.pop()
        return [line.decode('utf-8', errors='replace').rstrip('\r') for line in lines]


def summarize(rows, start, end, first_row):
    times = sorted(int(row['frame_time_us']) / 1000 for row in rows)
    if not times or times[0] < 0:
        raise ValueError('empty window or negative frame time')
    def percentile(p):
        return times[max(0, math.ceil(len(times) * p) - 1)]
    samples = sum(int(r.get('guest_frame_gpu_timing_samples', 0)) for r in rows)
    return dict(start_seconds=start, end_seconds=end, first_row=first_row,
                last_row=first_row + len(rows) - 1, frames=len(rows),
                median_ms=statistics.median(times), p95_ms=percentile(.95),
                p99_ms=percentile(.99), max_ms=times[-1],
                over_33ms=sum(v > 33.333 for v in times),
                over_50ms=sum(v > 50 for v in times),
                gpu_ms=(sum(int(r.get('guest_frame_gpu_time_ns', 0)) for r in rows)
                        / samples / 1e6) if samples else None,
                gpu_samples=samples,
                counters={key: sum(int(r.get(key, 0)) for r in rows) for key in (
                    'draw_calls', 'texture_dirty_load_attempts', 'pipeline_cache_misses',
                    'texture_request_cpu_time_ns', 'texture_request_timing_samples',
                    'resolve_readback_bytes', 'memexport_bytes', 'command_buffer_stalls',
                    'native_gpu_timing_drops')})


def write_report(output, windows, markers, status, warnings, coverage):
    worst = sorted(windows, key=lambda w: (w['p95_ms'], w['p99_ms']), reverse=True)[:20]
    report = dict(updated_utc=utc(), status=status, windows=len(windows),
                  worst_windows=worst, markers=markers, coverage=coverage,
                  warnings=sorted(warnings))
    atomic_json(output / 'report.json', report)
    lines = ['# Discovery session', '', f'Status: {status}. Updated: {utc()}.', '',
             'Instrumented discovery data, not a controlled performance benchmark.',
             'Window times use the CSV frame clock. Markers include UTC and the latest',
             'observed CSV row; CSV flush latency makes that association approximate.', '',
             '## Slowest recorded windows', '',
             '| CSV seconds | Median ms | p95 ms | p99 ms | GPU ms |',
             '| --- | ---: | ---: | ---: | ---: |']
    for w in worst:
        gpu = f"{w['gpu_ms']:.2f}" if w['gpu_ms'] is not None else 'unavailable'
        lines.append(f"| {w['start_seconds']:.1f}–{w['end_seconds']:.1f} | "
                     f"{w['median_ms']:.2f} | {w['p95_ms']:.2f} | {w['p99_ms']:.2f} | {gpu} |")
    lines += ['', f'Problem markers: {len(markers)}. See markers.jsonl for screenshots and timestamps.',
              '', 'Coverage: ' + json.dumps(coverage), '', '## Recording limits / caveats', '']
    lines += ['- ' + warning for warning in sorted(warnings)]
    staging = output / 'report.md.tmp'
    staging.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    staging.replace(output / 'report.md')


def record(args):
    kernel = ct.WinDLL('kernel32', use_last_error=True)
    user = ct.WinDLL('user32', use_last_error=True)
    user.SetProcessDPIAware()
    psapi = ct.WinDLL('psapi', use_last_error=True)
    kernel.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    kernel.OpenProcess.restype = wt.HANDLE
    kernel.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
    kernel.CloseHandle.argtypes = [wt.HANDLE]
    kernel.GetProcessTimes.argtypes = [wt.HANDLE] + [ct.POINTER(wt.FILETIME)] * 4
    user.GetForegroundWindow.restype = wt.HWND
    user.GetWindowThreadProcessId.argtypes = [wt.HWND, ct.POINTER(wt.DWORD)]
    user.GetWindowRect.argtypes = [wt.HWND, ct.POINTER(wt.RECT)]
    user.RegisterHotKey.argtypes = [wt.HWND, ct.c_int, wt.UINT, wt.UINT]
    user.UnregisterHotKey.argtypes = [wt.HWND, ct.c_int]
    user.PeekMessageW.argtypes = [ct.POINTER(wt.MSG), wt.HWND, wt.UINT, wt.UINT, wt.UINT]
    class Memory(ct.Structure):
        _fields_ = [('cb', wt.DWORD), ('faults', wt.DWORD)] + [
            (name, ct.c_size_t) for name in ('peak_working', 'working', 'peak_paged',
            'paged', 'peak_nonpaged', 'nonpaged', 'pagefile', 'peak_pagefile', 'private')]
    psapi.GetProcessMemoryInfo.argtypes = [wt.HANDLE, ct.POINTER(Memory), wt.DWORD]
    process = kernel.OpenProcess(0x100000 | 0x0400 | 0x0010, False, args.pid)
    if not process:
        raise ct.WinError(ct.get_last_error())
    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    warnings = {'Coverage counts are not shader cost or proof of native renderer retirement.',
                'Coverage samples one source frame in 60; brief effects can be missed. Counts are sampled, not whole-session totals.',
                'Marked screenshots may introduce hitches.',
                'Raw CSV stops at 512 MiB; sampled log archive stops at 256 MiB.',
                'Recorder stops after 12 hours; the game is never stopped by the recorder.'}
    try:
        from PIL import ImageGrab
    except ImportError:
        ImageGrab = None
        warnings.add('Pillow unavailable: timestamp markers work, screenshots disabled.')
    registered = []
    for key_id, vk in ((1, 0x77), (2, 0x78)):
        if not user.RegisterHotKey(None, key_id, 0x4000 | 0x0002 | 0x0004, vk):
            for previous in registered:
                user.UnregisterHotKey(None, previous)
            kernel.CloseHandle(process)
            raise RuntimeError('Ctrl+Shift+F8/F9 is already registered by another app')
        registered.append(key_id)
    atomic_json(output / 'recorder.json', dict(pid=args.pid, thread_id=kernel.GetCurrentThreadId(), started_utc=utc(),
                hotkeys=['Ctrl+Shift+F8: slowdown', 'Ctrl+Shift+F9: visual/timing'],
                screenshots=ImageGrab is not None))
    csv_tail = Tail()
    header = None
    csv_path = None
    existing_csv = set(json.loads((output / 'initial-perf-files.json').read_text()))
    session = None
    log_tails = {}
    for saved in json.loads((output / 'initial-log-offsets.json').read_text()):
        tail = Tail()
        tail.offset = saved['size']
        log_tails[tuple(saved['identity'])] = tail
    active_log_session = False
    log_bytes = 0
    screenshot_bytes = 0
    start = time.monotonic()
    last_report = 0
    row_number = 0
    elapsed = 0.0
    window_start = 0.0
    window_first = 0
    rows, windows, markers = [], [], []
    # The native renderer records no GPU execution corpus (bccf126).
    coverage = {'status': 'not recorded by the native renderer'}
    status = 'recording'
    files = {name: (output / (name + '.jsonl')).open('a', encoding='utf-8', buffering=1)
             for name in ('windows', 'markers', 'process')}
    archive = (output / 'samples.log').open('ab', buffering=65536)
    try:
        while True:
            alive = kernel.WaitForSingleObject(process, 0) == 258
            if csv_path is None:
                csv_path = find_session_csv(args.state_root / 'logs', args.pid, existing_csv)
                if csv_path is not None:
                    session = csv_path.name.removesuffix('.perf.csv')
                    atomic_json(output / 'session.json', dict(session=session, pid=args.pid,
                        csv=str(csv_path), state_root=str(args.state_root), started_utc=utc()))
            if csv_path is not None:
                for line in csv_tail.read(csv_path):
                    values = next(csv.reader([line]))
                    if header is None:
                        header = values
                        if 'frame_time_us' not in header:
                            raise ValueError('Unexpected performance CSV header')
                        continue
                    if len(values) != len(header):
                        raise ValueError('Malformed performance CSV row')
                    row = dict(zip(header, values))
                    duration = int(row['frame_time_us'])
                    if duration < 0:
                        raise ValueError('Negative frame duration')
                    elapsed += duration / 1e6
                    rows.append(row)
                    row_number += 1
                    # ponytail: 10-second bins localize issues; use raw CSV for exact analysis.
                    if elapsed - window_start >= 10 or len(rows) >= 10000:
                        window = summarize(rows, window_start, elapsed, window_first)
                        windows.append(window)
                        files['windows'].write(json.dumps(window) + '\n')
                        rows = []
                        window_start, window_first = elapsed, row_number
            # Preserve sampled timing lines before the existing bounded runtime log rotates.
            for path, stat in runtime_logs(args.state_root / 'logs'):
                identity = (stat.st_dev, stat.st_ino)
                if identity not in log_tails:
                    log_tails[identity] = Tail()
                tail = log_tails[identity]
                if log_bytes >= 256 * 1024 * 1024:
                    warnings.add('Sample log archive reached its cap; later samples omitted.')
                    break
                try:
                    lines = tail.read(path, limit=8 * 1024 * 1024, identity=identity)
                except (FileNotFoundError, PermissionError):
                    continue  # Normal log rotation or a transient Windows sharing lock.
                for line in lines:
                    if 'logging.ready' in line:
                        active_log_session = f'"pid":"{args.pid}"' in line
                    if active_log_session and 'CSV recording size limit reached' in line:
                        warnings.add('CSV cap reached: later markers have no new frame measurements.')
                    if active_log_session and ('FH1 ' in line or 'M2_EVENT' in line or 'IO_PROFILE ' in line or 'WAIT_PROFILE ' in line or 'CSV recording size limit' in line):
                        payload = (line + '\n').encode('utf-8')
                        if log_bytes + len(payload) <= 256 * 1024 * 1024:
                            archive.write(payload)
                            log_bytes += len(payload)
            archive.flush()
            # Keep rotation bookkeeping bounded. Open log files are never modified.
            if len(log_tails) > 100:
                log_tails = dict(list(log_tails.items())[-50:])
            message = wt.MSG()
            while user.PeekMessageW(ct.byref(message), None, 0, 0, 1):
                if message.message != 0x0312:
                    continue
                hwnd = user.GetForegroundWindow()
                foreground_pid = wt.DWORD()
                user.GetWindowThreadProcessId(hwnd, ct.byref(foreground_pid))
                if foreground_pid.value != args.pid or len(markers) >= 1000:
                    continue
                marker = dict(utc=utc(), recorder_seconds=time.monotonic() - start,
                    csv_seconds=elapsed, latest_csv_row=row_number - 1,
                    kind='slowdown' if message.wParam == 1 else 'visual-or-timing')
                if ImageGrab is not None and len(markers) < 100 and screenshot_bytes < 256 * 1024 * 1024:
                    rect = wt.RECT()
                    if user.GetWindowRect(hwnd, ct.byref(rect)) and rect.right > rect.left and rect.bottom > rect.top:
                        try:
                            screenshot = output / f'marker-{len(markers)+1:03d}.png'
                            ImageGrab.grab(bbox=(rect.left, rect.top, rect.right, rect.bottom), all_screens=True).save(screenshot)
                            screenshot_bytes += screenshot.stat().st_size
                            marker['screenshot'] = screenshot.name
                        except OSError as error:
                            marker['screenshot_error'] = str(error)
                markers.append(marker)
                files['markers'].write(json.dumps(marker) + '\n')
                user.MessageBeep(0)
            memory = Memory()
            memory.cb = ct.sizeof(memory)
            if psapi.GetProcessMemoryInfo(process, ct.byref(memory), ct.sizeof(memory)):
                times = [wt.FILETIME() for _ in range(4)]
                cpu = None
                if kernel.GetProcessTimes(process, *(ct.byref(value) for value in times)):
                    cpu = sum((value.dwHighDateTime << 32) | value.dwLowDateTime for value in times[2:]) / 1e7
                files['process'].write(json.dumps(dict(utc=utc(), seconds=time.monotonic()-start,
                    cpu_seconds=cpu, working_bytes=memory.working, private_bytes=memory.private,
                    io=process_io(kernel, process))) + '\n')
            if time.monotonic() - last_report >= 30:
                write_report(output, windows, markers, status, warnings, coverage)
                atomic_json(output / 'recorder-stats.json', dict(
                    elapsed_seconds=time.monotonic()-start, cpu_seconds=time.process_time(),
                    archived_log_bytes=log_bytes, screenshot_bytes=screenshot_bytes))
                last_report = time.monotonic()
            if not alive:
                # Drain the final CSV flush before reporting completion.
                if csv_path and csv_tail.offset < csv_path.stat().st_size:
                    continue
                status = 'game exited'
                break
            if time.monotonic() - start >= 12 * 3600:
                status = '12-hour recording limit reached; game left running'
                break
            time.sleep(1)
    except Exception as error:
        status = 'recorder failed; game left running'
        warnings.add(repr(error))
        raise
    finally:
        if rows:
            window = summarize(rows, window_start, elapsed, window_first)
            windows.append(window)
            files['windows'].write(json.dumps(window) + '\n')
        write_report(output, windows, markers, status, warnings, coverage)
        for stream in files.values():
            stream.close()
        archive.close()
        for key_id in registered:
            user.UnregisterHotKey(None, key_id)
        kernel.CloseHandle(process)


def self_test():
    import tempfile
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'tail'
        path.write_bytes(b'a\nb')
        tail = Tail()
        assert tail.read(path) == ['a']
        with path.open('ab') as stream:
            stream.write(b'c\n')
        assert tail.read(path) == ['bc']
        assert tail.read(path) == []
        path.write_bytes(b'other\n')
        assert Tail().read(path, identity=(-1, -1)) == []
        old_stat = path.stat()
        old_identity = (old_stat.st_dev, old_stat.st_ino)
        rotated = path.with_suffix('.1')
        path.rename(rotated)
        path.write_bytes(b'new session\n')
        assert Tail().read(path, identity=old_identity) == []
        assert Tail().read(rotated, identity=old_identity) == ['other']
        assert Tail().read(path) == ['new session']
        old_csv = Path(directory) / 'old-p42.perf.csv'
        old_csv.touch()
        assert find_session_csv(Path(directory), 42, {old_csv.name}) is None
        new_csv = Path(directory) / 'new-p42.perf.csv'
        new_csv.touch()
        assert find_session_csv(Path(directory), 42, {old_csv.name}) == new_csv
        try:
            find_session_csv(Path(directory), 42, set())
        except RuntimeError:
            pass
        else:
            raise AssertionError('ambiguous new sessions accepted')
        rows = [dict(frame_time_us=n, guest_frame_gpu_time_ns=2000000,
                     guest_frame_gpu_timing_samples=1) for n in (10000, 20000, 60000)]
        result = summarize(rows, 0, .09, 7)
        assert result['median_ms'] == 20 and result['p95_ms'] == 60
        assert result['gpu_ms'] == 2 and result['last_row'] == 9
        assert result['over_50ms'] == 1
        write_report(Path(directory), [result], [], 'test', set(), {})
        assert json.loads((Path(directory)/'report.json').read_text())['windows'] == 1
        if os.name == 'nt':
            kernel = ct.WinDLL('kernel32', use_last_error=True)
            kernel.GetCurrentProcess.restype = wt.HANDLE
            process = kernel.GetCurrentProcess()
            before = process_io(kernel, process)
            (Path(directory) / 'io-test.bin').write_bytes(b'x' * 4096)
            after = process_io(kernel, process)
            assert before is not None and after is not None
            assert after['write_bytes'] >= before['write_bytes'] + 4096
    print('discovery recorder checks passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int)
    parser.add_argument('--state-root', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--prepare', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    elif args.prepare:
        args.output.mkdir(parents=True, exist_ok=False)
        offsets = []
        for path, stat in runtime_logs(args.state_root / 'logs'):
            offsets.append(dict(identity=[stat.st_dev, stat.st_ino], size=stat.st_size))
        atomic_json(args.output / 'initial-log-offsets.json', offsets)
        atomic_json(args.output / 'initial-perf-files.json',
                    [p.name for p in (args.state_root / 'logs').glob('*.perf.csv')])
    elif not all((args.pid, args.state_root, args.output)):
        parser.error('--pid, --state-root and --output are required')
    else:
        record(args)
