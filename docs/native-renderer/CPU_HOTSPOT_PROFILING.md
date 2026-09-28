# CPU hotspot profiling

Use Windows Performance Recorder (WPR) sampling and context-switch data to
find the CPU work and waits that consume each source frame. The existing
source-frame boundary is also emitted through the
`PinyonShift-CriticalPath` TraceLogging provider, so sampled CPU and wait data
can be assigned to title frames without enabling the large diagnostic log.

## Capture a moving race route

Install Windows Performance Analyzer from the Windows Performance Toolkit,
then run an elevated PowerShell from the repository root:

```powershell
.\tools\capture-cpu-profile.ps1 `
  -StateRoot .local/render-seeds/appdata-2026-09-27 `
  -ShaderPack <1x shaders.pnsp> `
  -RenderTestScript config/render-tests/fh1-race-sync.fh1test
```

The capture runs the game on a private copy of `-StateRoot` (its `user`,
`config` and shader catalogs) under the capture directory, so a pinned seed
or the AppData save is never written. Pass the shader pack the route was
validated with: without one, every pack miss drops its draw and the profile
does not represent a real frame.

`config/render-tests/fh1-race-sync.fh1test` waits on game state instead of
wall time and ends in a moving race (its last 600 frames; see the
[performance baselines](NATIVE_PERFORMANCE_BASELINES.md)), but it reaches
that race only from a save in the state of seed `appdata-2026-09-27`. Keep
full-resolution screenshots outside the measured source-frame window.

The performance CSV still has the columns `fh1_issue_draw_cpu_time_ns`,
`fh1_prepared_snapshot_cpu_time_ns`, `fh1_prepared_observer_cpu_time_ns`,
`fh1_binding_cpu_time_ns`, `fh1_issue_draw_calls`,
`fh1_material_snapshot_cpu_time_ns`, `fh1_material_snapshot_calls`,
`fh1_native_output_cpu_time_ns`, `fh1_title_thread_cpu_time_ns` and
`fh1_gpu_thread_cpu_time_ns`. They were written by the removed native race
pilot and its draw observers (`a75be82`, `6b75238`) and stay zero on the
current build; use the sampled trace for CPU attribution. The executor's own
GPU phase times come from `--fh1_native_gpu_profile=true` in its periodic
stats, including `texture_reloads`, the untile and copy of texture data a
resolve invalidated. Every frame, `texture_resolve_reloads` and
`texture_resolve_reload_bytes` count those reloads and the guest bytes they
read; `texture_dirty_load_attempts` counts all texture reloads.

The script verifies the AppData save, rejects an already-running game, builds
`RelWithDebInfo`, checks the title, generated guest facades, and ShiftGlue
binaries against their PDBs, records a focused kernel
profile (sampled CPU, context switches, ready threads, processes, and image
loads) plus the project TraceLogging provider, runs
the selected render-test route, and saves the ETL, symbols, frame CSV,
and capture manifest below `.local/cpu-profile`.
The command fails and marks the manifest invalid if WPR reports any dropped
events; do not analyze that ETL.

The build can be prepared without elevation:

```powershell
.\tools\build-preview.ps1 -Configuration RelWithDebInfo
.\tools\verify-profile-symbols.ps1 -Json
```

Use `-SkipBuild` for repeat captures. `-MarkersOnly` records only project
events, which is useful when checking event fields but cannot identify CPU
hotspots. Both modes require an elevated WPR session on standard Windows
installations.

## Analyze the ETL

Open `pinyon-shift.etl` in WPA and add these tables:

1. **CPU Usage (Sampled)**, grouped by process, thread, module, function, and
   stack. Restrict the process to `pinyon_shift.exe` and load symbols from the
   capture's `symbols` directory.
2. **CPU Usage (Precise)**, grouped by process, thread, wait reason, and stack.
   Use this view to distinguish blocked time from scheduler delay. The CSV
   exporter below includes blocked intervals only.
3. **Generic Events**, restricted to provider
   `PinyonShift-CriticalPath`. `SourceFrame` identifies title frame boundaries;
   `CriticalPath` gives the consumed-swap ordinal, other existing phase
   boundaries, and render-test input ticks on the same ETW clock. Producer and
   consumed swap IDs pair in order; the latest-frame snapshot on asynchronous
   submission or present is not proof that it consumed that frame.

Verify that title, generated guest facades, `rexruntimerd`, and `rexgpu-fh1rd`
stacks show function names where those modules have samples. An address-only
stack is a failed symbol check and must not be used to justify an optimization.

To export the ETL directly with Microsoft's TraceEvent reader:

```powershell
dotnet run --project tools/profile-etl-export -- `
  .local/cpu-profile/<capture-directory>
```

This writes `markers.csv`, `critical-path.csv`, `samples.csv`, and `waits.csv`
beside the ETL and
fails if the capture lost events or more than 1% of game CPU samples lack
stacks. The same CSV contract can also be produced from WPA:

```text
markers.csv: timestamp_ms,source_frame,thread_id
critical-path.csv: timestamp_ms,event,source_frame,thread_id,value0,value1,value2
samples.csv: timestamp_ms,cpu_ms,module,function,thread_id,project_caller,ip,rva,stack
waits.csv:   timestamp_ms,wait_ms,wait_reason,thread_id
```

Then run:

```powershell
python tools/summarize-cpu-hotspots.py markers.csv samples.csv `
  --waits waits.csv --start-frame 4200 --end-frame 4590 `
  --output race-hotspots.json
```

Choose a contiguous range from `markers.csv`; the frame numbers above reproduce
the 2026-09-21 **pre-driving** sample. Script ticks and SourceFrame IDs have
different clocks. Use `render_test_input` rows in `critical-path.csv` to find
the actual throttle interval in a new capture.
Use `tools/summarize-drive-window.py` with the same run's performance CSV and
diagnostic JSONL to measure consumed swaps between the `race-moving` and
`race-sustained` captures. This reports both wall and title simulation time,
plus vehicle displacement, so A/B runs with different scenes can be rejected.
The script assigns every sample and wait to the latest preceding source-frame
marker and writes both JSON and Markdown, ranked by total sampled CPU or wait
time. The JSON includes individual frame CPU and wait totals. Both metrics add
time across concurrent game threads; neither is wall-clock frame latency.
Waits include idle workers. Keep the ETL beside the report so stacks can be
inspected before changing code. The first measured results, taken on the
Xenos renderer before its removal, are summarized in the
[Xenos retirement backlog](XENOS_RETIREMENT_BACKLOG.md#target-architecture);
the full report is archived as `CPU_HOTSPOT_RESULTS_2026-09-21.md` at
checkpoint `02dfad0` (see the [research reference](RESEARCH.md#recovering-exact-historical-evidence)).

## Escalate only when the trace calls for it

Use PIX timing capture when the sampled trace points to D3D12 submission,
present, or fence waits and CPU/GPU overlap needs to be measured. RenderDoc is
for a concrete graphics-state or resource-content investigation; its capture
overhead and single-frame focus do not make it a CPU frame profiler.

Microsoft references:

- [Capture and view TraceLogging data](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/capture-and-view-tracelogging-data)
- [Author recording profiles](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/authoring-recording-profiles)
- [CPU analysis](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis)
- [Symbol support](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/symbol-support)
