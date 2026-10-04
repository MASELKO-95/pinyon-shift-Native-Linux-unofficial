using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace PinyonShift.Launcher.Linux;

public sealed record LauncherOptions(string Repository, string State, string? Screenshot, bool ShowSettings)
{
    public static LauncherOptions Parse(string[] args)
    {
        string? root = null, state = null, screenshot = null;
        bool settings = false;
        for (int i = 0; i < args.Length; i++)
        {
            if (args[i] == "--show-settings") { settings = true; continue; }
            if (args[i] is not ("--repo-root" or "--state-root" or "--screenshot") || i + 1 == args.Length)
                throw new ArgumentException($"Unknown option or missing value: {args[i]}");
            string key = args[i], value = Path.GetFullPath(args[++i]);
            if (key == "--repo-root") root = value;
            else if (key == "--state-root") state = value;
            else screenshot = value;
        }
        root ??= Discover(AppContext.BaseDirectory) ?? Discover(Environment.CurrentDirectory);
        if (root is null || !File.Exists(Path.Combine(root, "tools", "setup-preview.sh")))
            throw new ArgumentException("Source checkout not found. Start tools/launch-launcher.sh or pass --repo-root DIR.");
        state ??= Environment.GetEnvironmentVariable("PINYON_SHIFT_STATE_ROOT");
        return new(root, Path.GetFullPath(string.IsNullOrWhiteSpace(state)
            ? Path.Combine(root, ".local", "preview") : state), screenshot, settings);
    }

    private static string? Discover(string start)
    {
        for (var current = new DirectoryInfo(start); current is not null; current = current.Parent)
            if (File.Exists(Path.Combine(current.FullName, "tools", "setup-preview.sh"))) return current.FullName;
        return null;
    }
}

public sealed record StageEvent(string Stage, int Percent, string Message)
{
    public static StageEvent? Parse(string line)
    {
        if (!line.StartsWith("::pinyon::", StringComparison.Ordinal)) return null;
        try
        {
            using var doc = JsonDocument.Parse(line[10..]);
            var root = doc.RootElement;
            return new(root.GetProperty("stage").GetString() ?? "",
                Math.Clamp(root.GetProperty("percent").GetInt32(), 0, 100),
                root.GetProperty("message").GetString() ?? "");
        }
        catch (Exception e) when (e is JsonException or KeyNotFoundException or InvalidOperationException or FormatException)
        { return null; }
    }
}

public sealed record ProcessResult(int ExitCode, string Output);

public static class ProcessRunner
{
    public static async Task<ProcessResult> RunAsync(string file, IEnumerable<string> arguments,
        string directory, Action<string>? output = null, CancellationToken cancellation = default)
    {
        var start = new ProcessStartInfo(file)
        {
            WorkingDirectory = directory, UseShellExecute = false,
            RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true
        };
        foreach (string argument in arguments) start.ArgumentList.Add(argument);
        start.Environment["PYTHONUNBUFFERED"] = "1";
        using var process = new Process { StartInfo = start };
        cancellation.ThrowIfCancellationRequested();
        process.Start();
        var text = new StringBuilder();
        object gate = new();
        async Task ReadAsync(StreamReader stream)
        {
            while (await stream.ReadLineAsync() is { } line)
            {
                lock (gate)
                {
                    // A bounded tail is enough for JSON results and error messages.
                    text.AppendLine(line);
                    if (text.Length > 128_000) text.Remove(0, text.Length - 96_000);
                    output?.Invoke(line);
                }
            }
        }
        using var registration = cancellation.Register(() =>
        {
            try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
            catch (InvalidOperationException) { }
        });
        await Task.WhenAll(ReadAsync(process.StandardOutput), ReadAsync(process.StandardError),
                           process.WaitForExitAsync());
        cancellation.ThrowIfCancellationRequested();
        return new(process.ExitCode, text.ToString());
    }
}

public sealed class LauncherBackend(LauncherOptions options)
{
    public LauncherOptions Options { get; } = options;
    public Publication? Distribution
    {
        get
        {
            try
            {
                using var doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(Options.Repository, "config/linux-publication.json")));
                string? repository = doc.RootElement.GetProperty("repository").GetString();
                string? maintainer = doc.RootElement.GetProperty("maintainer").GetString();
                return repository is not null && Regex.IsMatch(repository, @"\A[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\z")
                    ? new(repository, maintainer ?? repository.Split('/')[0]) : null;
            }
            catch (Exception error) when (error is IOException or JsonException or KeyNotFoundException or InvalidOperationException)
            { return null; }
        }
    }
    public string BuildDirectory => Path.Combine(Options.Repository, "out/build/linux-amd64-release");
    public string GameDirectory => Path.Combine(Options.Repository, ".local/game/base");
    public IReadOnlyList<DiscEdition> DiscEditions()
    {
        var editions = new List<DiscEdition> { new("", "Auto-detect supported retail disc", false, "") };
        foreach (var (name, experimental) in new[] { ("supported-dumps.json", false), ("experimental-dumps.json", true) })
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(Options.Repository, "config", name)));
            foreach (var dump in doc.RootElement.GetProperty("dumps").EnumerateArray())
            {
                string id = dump.GetProperty("id").GetString()!;
                string region = dump.GetProperty("region").GetString()!;
                string hash = dump.GetProperty("executables").EnumerateArray()
                    .First(exe => exe.GetProperty("guest_path").GetString() == "default.xex")
                    .GetProperty("sha256").GetString()!;
                editions.Add(new(id, region + (experimental ? " — experimental" : " — retail"), experimental, hash));
            }
        }
        return editions;
    }
    // Paths belong to the private checkout, never to the publication manifest.
    public string? SavedIsoPath()
    {
        try
        {
            string file = Path.Combine(Options.Repository, ".local/setup-state.json");
            if (File.Exists(file))
            {
                using var doc = JsonDocument.Parse(File.ReadAllText(file));
                if (doc.RootElement.TryGetProperty("iso_path", out var path) &&
                    path.GetString() is { Length: > 0 } saved) return saved;
            }
            string images = Path.Combine(Options.Repository, ".local/images");
            if (!Directory.Exists(images)) return null;
            // Offer a single local image; multiple images require an explicit choice.
            var candidates = Directory.EnumerateFiles(images).Where(path =>
                Path.GetExtension(path).Equals(".iso", StringComparison.OrdinalIgnoreCase)).Take(2).ToArray();
            return candidates.Length == 1 ? candidates[0] : null;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException or InvalidOperationException)
        { return null; }
    }

    public IReadOnlyList<GameLanguage> GameLanguages()
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(Options.Repository, "config/linux-game-languages.json")));
        return new[] { new GameLanguage("", "Keep current game language", 0, 0) }.Concat(
            doc.RootElement.GetProperty("languages").EnumerateArray().Select(item => new GameLanguage(
                item.GetProperty("id").GetString()!, item.GetProperty("label").GetString()!,
                item.GetProperty("user_language").GetInt32(), item.GetProperty("user_country").GetInt32()))).ToArray();
    }
    public bool HasGame => File.Exists(Path.Combine(GameDirectory, "default.xex"));
    public bool IsReady => HasGame && new[] { "pinyon_shift", "librexruntime.so", "librexgpu-fh1.so",
        "libpinyon_shift_SpeechFacade_default.so", "libpinyon_shift_XMediaFacade_default.so" }
        .All(name => File.Exists(Path.Combine(BuildDirectory, name)));
    public string Version
    {
        get
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(Options.Repository, "config/linux-port.json")));
            return doc.RootElement.GetProperty("version").GetString() ?? "Linux preview";
        }
    }
    public static bool GameRunning()
    {
        var processes = Process.GetProcessesByName("pinyon_shift");
        bool running = processes.Length != 0;
        foreach (var process in processes) process.Dispose();
        return running;
    }
    public Task<ProcessResult> ScriptAsync(string script, IEnumerable<string> arguments,
        Action<string>? output = null, CancellationToken cancellation = default) =>
        ProcessRunner.RunAsync("bash", new[] { Path.Combine(Options.Repository, "tools", script) }.Concat(arguments),
            Options.Repository, output, cancellation);

    public async Task<GraphicsSettings> ReadSettingsAsync()
    {
        var result = await ScriptAsync("set-graphics-experiment.sh", ["--state-root", Options.State, "--json"]);
        if (result.ExitCode != 0) throw new IOException(result.Output);
        using var doc = JsonDocument.Parse(result.Output);
        return GraphicsSettings.Parse(doc.RootElement.GetProperty("config").GetString() ?? "");
    }

    public async Task<(int Language, int Country, bool Trainer)> ReadGameOptionsAsync()
    {
        var result = await ScriptAsync("set-graphics-experiment.sh", ["--state-root", Options.State, "--json"]);
        if (result.ExitCode != 0) throw new IOException(result.Output);
        using var doc = JsonDocument.Parse(result.Output);
        string config = doc.RootElement.GetProperty("config").GetString() ?? "";
        int Number(string name)
        {
            var match = Regex.Match(config, @"(?m)^\s*" + name + @"\s*=\s*(\d+)");
            return match.Success ? int.Parse(match.Groups[1].Value) : 0;
        }
        return (Number("user_language"), Number("user_country"),
            Regex.IsMatch(config, @"(?m)^\s*pinyon_shift_cheats\s*=\s*true\s*(?:#.*)?$"));
    }

    public string? PendingReport()
    {
        string file = Path.Combine(Options.State, "reports/pending-report.json");
        if (!File.Exists(file)) return null;
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(file));
            string? bundle = doc.RootElement.GetProperty("bundle").GetString();
            if (bundle is null) return null;
            string full = Path.GetFullPath(bundle);
            string reports = Path.GetFullPath(Path.Combine(Options.State, "reports")) + Path.DirectorySeparatorChar;
            return full.StartsWith(reports, StringComparison.Ordinal) && File.Exists(full) ? full : null;
        }
        catch (Exception e) when (e is IOException or JsonException or KeyNotFoundException or ArgumentException or InvalidOperationException)
        { return null; }
    }
}

public sealed record Publication(string Repository, string Maintainer)
{
    public string Url => $"https://github.com/{Repository}";
}

public sealed record DiscEdition(string Id, string Label, bool Experimental, string ExecutableHash)
{
    public override string ToString() => Label;
}

public sealed record GameLanguage(string Id, string Label, int Language, int Country)
{
    public override string ToString() => Label;
}

public sealed record GraphicsSettings(int Scale = 1, string Scaling = "bilinear", bool TreasureMap = true, int RenderFps = 0)
{
    public static GraphicsSettings Parse(string config)
    {
        string Value(string name, string fallback)
        {
            var match = Regex.Match(config, @"(?m)^\s*" + Regex.Escape(name) + @"\s*=\s*([^\r\n#]+)");
            return match.Success ? match.Groups[1].Value.Trim().Trim('"') : fallback;
        }
        int.TryParse(Value("draw_resolution_scale_x", "1"), out int scale);
        int.TryParse(Value("pinyon_shift_fh1_render_fps_limit", "0"), out int renderFps);
        return new(Math.Clamp(scale, 1, 4), Value("present_effect", "bilinear"),
                   Value("pinyon_shift_dlc_treasure_map", "true") == "true", Math.Clamp(renderFps, 0, 240));
    }
    public string[] Arguments(string state) => ["--action", "apply", "--state-root", state,
        "--resolution-scale", Scale.ToString(), "--output-scaling", Scaling,
        "--treasure-map", TreasureMap ? "true" : "false", "--render-fps", RenderFps.ToString(), "--json"];
}
