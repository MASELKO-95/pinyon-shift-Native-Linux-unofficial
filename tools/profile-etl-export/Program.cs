using System.Globalization;
using System.Text.Json;
using Microsoft.Diagnostics.Symbols;
using Microsoft.Diagnostics.Tracing;
using Microsoft.Diagnostics.Tracing.Etlx;

if (args.Length != 1)
    throw new ArgumentException("Usage: dotnet run --project tools/profile-etl-export -- <capture-directory>");

var directory = Path.GetFullPath(args[0]);
using var manifest = JsonDocument.Parse(File.ReadAllText(Path.Combine(directory, "capture.json")));
if (!manifest.RootElement.GetProperty("valid").GetBoolean() ||
    manifest.RootElement.GetProperty("dropped_events").GetInt64() != 0)
    throw new InvalidDataException("Capture contains dropped events");
var markersOnly = manifest.RootElement.GetProperty("markers_only").GetBoolean();
using var launch = JsonDocument.Parse(File.ReadAllText(Path.Combine(directory, "launch.json")));
var processId = launch.RootElement.GetProperty("process_id").GetInt32();
var etl = Path.Combine(directory, "pinyon-shift.etl");
using var log = TraceLog.OpenOrConvert(etl);
if (log.EventsLost != 0 || (!markersOnly && !log.HasCallStacks))
    throw new InvalidDataException("ETL has lost events or no call stacks");

var projectModules = new HashSet<string>(StringComparer.OrdinalIgnoreCase) {
    "pinyon_shift", "pinyon_shift_SpeechFacade_default",
    "pinyon_shift_XMediaFacade_default", "rexruntimerd", "rexgpu-fh1rd"
};
if (!markersOnly) {
    using var symbolLog = new StreamWriter(Path.Combine(directory, "symbol-resolution.log"));
    using var symbols = new SymbolReader(symbolLog, Path.Combine(directory, "symbols"), null);
    foreach (var module in log.ModuleFiles) {
        if (projectModules.Contains(module.Name))
            log.CodeAddresses.LookupSymbolsForModule(symbols, module);
    }
}

using var markers = new StreamWriter(Path.Combine(directory, "markers.csv"));
using var criticalPath = new StreamWriter(Path.Combine(directory, "critical-path.csv"));
using var samples = new StreamWriter(Path.Combine(directory, "samples.csv"));
using var waits = new StreamWriter(Path.Combine(directory, "waits.csv"));
markers.WriteLine("timestamp_ms,source_frame,thread_id");
criticalPath.WriteLine("timestamp_ms,event,source_frame,thread_id,value0,value1,value2");
samples.WriteLine("timestamp_ms,cpu_ms,module,function,thread_id,project_caller,ip,rva,stack");
waits.WriteLine("timestamp_ms,wait_ms,wait_reason,thread_id");
var waiting = new Dictionary<int, (double start, string reason)>();
var markerCount = 0;
var sampleCount = 0;
var waitCount = 0;
var missingStacks = 0;
var source = log.Events.GetSource();
var providerGuid = Guid.Parse("f36ab1a6-80bb-4482-b1b9-92a6ec870258");
source.Dynamic.All += data => {
    if (data.ProviderGuid != providerGuid || data.ProcessID != processId) return;
    if (data.EventName == "SourceFrame") {
        markers.WriteLine($"{Number(data.TimeStampRelativeMSec)},{data.PayloadByName("SourceFrame")},{data.ThreadID}");
        markerCount++;
    } else if (data.EventName == "CriticalPath") {
        Csv(criticalPath, Number(data.TimeStampRelativeMSec),
            data.PayloadByName("Event")?.ToString() ?? "",
            data.PayloadByName("SourceFrame")?.ToString() ?? "", data.ThreadID.ToString(CultureInfo.InvariantCulture),
            data.PayloadByName("Value0")?.ToString() ?? "", data.PayloadByName("Value1")?.ToString() ?? "",
            data.PayloadByName("Value2")?.ToString() ?? "");
    }
};
source.Kernel.PerfInfoSample += data => {
    if (data.ProcessID != processId) return;
    var stack = data.CallStack();
    if (stack == null) missingStacks++;
    var leaf = stack?.CodeAddress;
    var module = leaf?.ModuleName ?? "<unknown>";
    var function = leaf?.FullMethodName;
    if (string.IsNullOrEmpty(function)) function = "<unknown>";
    var projectCaller = "";
    var stackNames = new List<string>();
    for (var frame = stack; frame != null; frame = frame.Caller) {
        stackNames.Add($"{frame.CodeAddress.ModuleName}!{frame.CodeAddress.FullMethodName}");
        if (!projectModules.Contains(frame.CodeAddress.ModuleName)) continue;
        if (projectCaller == "")
            projectCaller = stackNames[^1];
    }
    var imageBase = leaf?.ModuleFile?.ImageBase ?? 0;
    var rva = leaf != null && imageBase != 0 && leaf.Address >= imageBase
        ? $"0x{leaf.Address - imageBase:X}" : "";
    Csv(samples, Number(data.TimeStampRelativeMSec), Number(log.SampleProfileInterval.TotalMilliseconds),
        module, function, data.ThreadID.ToString(CultureInfo.InvariantCulture), projectCaller,
        leaf == null ? "" : $"0x{leaf.Address:X}", rva, string.Join(" <- ", stackNames));
    sampleCount++;
};
source.Kernel.ThreadCSwitch += data => {
    if (data.OldProcessID != processId || data.OldThreadState != System.Diagnostics.ThreadState.Wait) return;
    waiting[data.OldThreadID] = (data.TimeStampRelativeMSec, data.OldThreadWaitReason.ToString());
};
source.Kernel.DispatcherReadyThread += data => {
    if (data.AwakenedProcessID != processId || !waiting.Remove(data.AwakenedThreadID, out var blocked)) return;
    var duration = data.TimeStampRelativeMSec - blocked.start;
    if (duration < 0) return;
    Csv(waits, Number(blocked.start), Number(duration), blocked.reason,
        data.AwakenedThreadID.ToString(CultureInfo.InvariantCulture));
    waitCount++;
};
source.Process();
Console.WriteLine($"markers={markerCount} samples={sampleCount} waits={waitCount} missing_stacks={missingStacks} lost={log.EventsLost}");
if (markerCount == 0 || (!markersOnly &&
    (sampleCount == 0 || missingStacks > sampleCount / 100)))
    throw new InvalidDataException("Markers or samples missing, or more than 1% of game samples lack stacks");

static string Number(double value) => value.ToString("0.####", CultureInfo.InvariantCulture);
static void Csv(TextWriter writer, params string[] fields) =>
    writer.WriteLine(string.Join(",", fields.Select(field => $"\"{field.Replace("\"", "\"\"")}\"")));
