using Microsoft.Win32;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace PinyonShift.Launcher;

public partial class MainWindow : Window
{
    private readonly ObservableCollection<RouteStep> _steps =
    [
        new("VERIFY", "Disc image", "Exact size and SHA-256", "1"),
        new("TOOLS", "Windows toolchain", "Provisioned when missing", "2"),
        new("EXTRACT", "Local game files", "Never uploaded or modified", "3"),
        new("BUILD", "Local preparation", "Game and graphics built here", "4"),
        new("PLAY", "Ready to drive", "Launch from this screen", "5")
    ];

    // The content area shows one of these at a time.
    private enum View { Setup, Ready, Log, Crash, Graphics }

    private CancellationTokenSource? _cancellation;
    private string? _repositoryRoot;
    private string? _stateRoot;
    private string? _gameExecutable;
    private StreamWriter? _sessionLog;
    private CrashReport? _pendingReport;
    private bool _busy;
    private bool _canChooseInstallRoot;
    private View _panel = View.Setup;
    private View _panelBeforeGraphics = View.Setup;

    private static readonly string InstallRootPreference = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "PinyonShift", "install-root.txt");

    private static readonly Brush WaitingBrush = new SolidColorBrush(Color.FromRgb(52, 73, 59));
    private static readonly Brush ActiveBrush = new SolidColorBrush(Color.FromRgb(241, 174, 54));
    private static readonly Brush CompleteBrush = new SolidColorBrush(Color.FromRgb(92, 208, 138));
    private static readonly Brush FailedBrush = new SolidColorBrush(Color.FromRgb(225, 110, 95));

    public MainWindow()
    {
        InitializeComponent();
        RouteList.ItemsSource = _steps;
        BuildLocationText.Text = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PinyonShift");
        VersionText.Text = $"Pinyon Shift launcher {typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "dev"}";
        Loaded += MainWindow_Loaded;
        StateChanged += (_, _) =>
        {
            // A maximized borderless window reaches past the screen edge by the
            // resize border; pad it back in.
            RootBorder.Margin = WindowState == WindowState.Maximized ? new Thickness(7) : new Thickness(0);
            // Two overlapping squares while maximized, one otherwise.
            MaximizeGlyph.Data = Geometry.Parse(WindowState == WindowState.Maximized
                ? "M2.5,0.5 H9.5 V7.5 M0.5,2.5 H7.5 V9.5 H0.5 Z"
                : "M0.5,0.5 H9.5 V9.5 H0.5 Z");
        };
        Closing += (_, _) =>
        {
            _cancellation?.Cancel();
            _sessionLog?.Dispose();
        };
    }

    private async void MainWindow_Loaded(object sender, RoutedEventArgs e)
        => await InitializeSourceAsync();

    private async Task InitializeSourceAsync(string? installRoot = null)
    {
        _busy = true;
        IsEnabled = false;
        try
        {
            var repositoryRoot = await ResolveRepositoryRootAsync(installRoot);
            var stateRoot = ResolveStateRoot(repositoryRoot);
            if (installRoot is not null)
            {
                Directory.CreateDirectory(Path.GetDirectoryName(InstallRootPreference)!);
                await File.WriteAllTextAsync(InstallRootPreference, installRoot);
            }
            _sessionLog?.Dispose();
            _sessionLog = null;
            ResetRoute();
            SetReadyState();
            _repositoryRoot = repositoryRoot;
            _stateRoot = stateRoot;
            StartSessionLog(_repositoryRoot);
            ShowReleaseVersion();
            GraphicsSettingsButton.Visibility = Visibility.Visible;
            BuildLocationText.Text = _stateRoot;
            StateRootText.Text = _stateRoot;
            AppendLog($"Release source: {_repositoryRoot}");
            AppendLog($"Preview state: {_stateRoot}");
            StageControllerMappings();
            DetectExistingBuild();
            DetectPendingReport();
            UpdatePrimaryButton();
        }
        catch (Exception ex)
        {
            SetFailure("SOURCE UNAVAILABLE", ex.Message);
        }
        finally
        {
            _busy = false;
            IsEnabled = true;
            UpdatePrimaryButton();
        }
    }

    private void ShowReleaseVersion()
    {
        if (_repositoryRoot is null) return;
        try
        {
            using var release = JsonDocument.Parse(File.ReadAllText(Path.Combine(_repositoryRoot, "config", "release.json")));
            var version = release.RootElement.GetProperty("version").GetString();
            var channel = release.RootElement.TryGetProperty("channel", out var value) ? value.GetString() : null;
            if (!string.IsNullOrWhiteSpace(version))
                VersionText.Text = $"Pinyon Shift {version}";
            ChannelText.Text = string.IsNullOrWhiteSpace(channel) ? "" : channel.ToUpperInvariant();
        }
        catch (Exception ex) when (ex is IOException or JsonException or KeyNotFoundException or InvalidOperationException) { }
    }

    private async void ChooseInstallRootButton_Click(object sender, RoutedEventArgs e)
    {
        if (_busy || !_canChooseInstallRoot) return;
        var dialog = new OpenFolderDialog
        {
            Title = "Choose installation folder (existing installations and saves stay in place)",
            Multiselect = false
        };
        if (dialog.ShowDialog(this) == true)
            await InitializeSourceAsync(dialog.FolderName);
    }

    private void BrowseButton_Click(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog
        {
            Title = "Choose your Forza Horizon disc image",
            Filter = "Xbox 360 disc image (*.iso)|*.iso|All files (*.*)|*.*",
            CheckFileExists = true,
            Multiselect = false
        };
        if (dialog.ShowDialog(this) == true)
            SelectDiscImage(dialog.FileName);
        UpdatePrimaryButton();
    }

    private void SelectDiscImage(string path)
    {
        IsoPathTextBox.Text = path;
        DropHintText.Text = Path.GetFileName(path);
        ResetRoute();
        SetReadyState();
        ShowPanel(View.Setup);
        UpdatePrimaryButton();
    }

    // Dragging a disc image anywhere onto the window selects it.
    private bool CanAcceptDrop(DragEventArgs e, out string? path)
    {
        path = null;
        if (_busy || _gameExecutable is not null || _pendingReport is not null ||
            !e.Data.GetDataPresent(DataFormats.FileDrop)) return false;
        if (e.Data.GetData(DataFormats.FileDrop) is not string[] { Length: 1 } files) return false;
        path = files[0];
        return File.Exists(path);
    }

    private void Window_DragOver(object sender, DragEventArgs e)
    {
        var accepted = CanAcceptDrop(e, out _);
        e.Effects = accepted ? DragDropEffects.Copy : DragDropEffects.None;
        DropOverlay.Visibility = accepted ? Visibility.Visible : Visibility.Collapsed;
        e.Handled = true;
    }

    protected override void OnDragLeave(DragEventArgs e)
    {
        base.OnDragLeave(e);
        DropOverlay.Visibility = Visibility.Collapsed;
    }

    private void Window_Drop(object sender, DragEventArgs e)
    {
        DropOverlay.Visibility = Visibility.Collapsed;
        if (CanAcceptDrop(e, out var path) && path is not null)
            SelectDiscImage(path);
        e.Handled = true;
    }

    private void InputChanged(object sender, RoutedEventArgs e) => UpdatePrimaryButton();

    private async void PrimaryButton_Click(object sender, RoutedEventArgs e)
    {
        if (_pendingReport is not null)
        {
            ReportCrash();
            return;
        }
        if (_gameExecutable is not null && File.Exists(_gameExecutable))
        {
            await LaunchGameAsync();
            return;
        }

        if (_busy || _repositoryRoot is null)
            return;

        _busy = true;
        ChooseInstallRootButton.IsEnabled = false;
        GraphicsSettingsButton.IsEnabled = false;
        _cancellation = new CancellationTokenSource();
        BrowseButton.IsEnabled = false;
        OwnershipCheckBox.IsEnabled = false;
        PrimaryButton.IsEnabled = false;
        SetPrimaryText("BUILDING…");
        ShowPanel(View.Log);
        SetProgress(0, "Starting local setup.");
        HeadlineText.Text = "Preparing the road.";
        SubheadText.Text = "The first build compiles the whole game on this PC and can take a while. You can leave it running.";
        EyebrowText.Text = "LOCAL BUILD IN PROGRESS";
        StatusText.Text = "WORKING";
        StatusDot.Fill = ActiveBrush;
        AppendLog("Starting local setup. The first build can take a while.");

        try
        {
            var script = Path.Combine(_repositoryRoot, "tools", "setup-preview.ps1");
            if (!File.Exists(script))
                throw new FileNotFoundException("The setup workflow is missing from the release payload.", script);

            var startInfo = new ProcessStartInfo
            {
                FileName = "powershell.exe",
                WorkingDirectory = _repositoryRoot,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            };
            startInfo.ArgumentList.Add("-NoLogo");
            startInfo.ArgumentList.Add("-NoProfile");
            startInfo.ArgumentList.Add("-ExecutionPolicy");
            startInfo.ArgumentList.Add("Bypass");
            startInfo.ArgumentList.Add("-File");
            startInfo.ArgumentList.Add(script);
            startInfo.ArgumentList.Add("-IsoPath");
            startInfo.ArgumentList.Add(IsoPathTextBox.Text);
            startInfo.ArgumentList.Add("-JsonEvents");

            using var process = new Process { StartInfo = startInfo, EnableRaisingEvents = true };
            process.OutputDataReceived += (_, args) => Dispatcher.Invoke(() => HandleOutput(args.Data));
            process.ErrorDataReceived += (_, args) => Dispatcher.Invoke(() =>
            {
                if (!string.IsNullOrWhiteSpace(args.Data)) AppendLog(args.Data);
            });
            if (!process.Start())
                throw new InvalidOperationException("Windows could not start the setup process.");
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();

            using var registration = _cancellation.Token.Register(() =>
            {
                try { if (!process.HasExited) process.Kill(entireProcessTree: true); } catch { }
            });
            await process.WaitForExitAsync(_cancellation.Token);
            if (process.ExitCode != 0)
                throw new InvalidOperationException("Setup stopped before completing. The details above contain the cause.");

            DetectExistingBuild();
            if (_gameExecutable is null)
                throw new InvalidOperationException("Setup completed without producing the expected game executable.");
            SetComplete();
        }
        catch (OperationCanceledException)
        {
            SetFailure("BUILD CANCELLED", "No game or source files were uploaded. Run the launcher again to resume.");
        }
        catch (Exception ex)
        {
            SetFailure("SETUP NEEDS ATTENTION", ex.Message);
        }
        finally
        {
            _busy = false;
            GraphicsSettingsButton.IsEnabled = true;
            BrowseButton.IsEnabled = true;
            OwnershipCheckBox.IsEnabled = true;
            UpdatePrimaryButton();
        }
    }

    private void HandleOutput(string? line)
    {
        if (string.IsNullOrWhiteSpace(line)) return;
        const string prefix = "::pinyon::";
        if (!line.StartsWith(prefix, StringComparison.Ordinal))
        {
            AppendLog(line);
            return;
        }

        try
        {
            var message = JsonSerializer.Deserialize<ProgressMessage>(line[prefix.Length..], new JsonSerializerOptions
            {
                PropertyNameCaseInsensitive = true
            });
            if (message is null) return;
            var stage = string.Equals(message.Stage, "shaders", StringComparison.OrdinalIgnoreCase)
                ? "build" : message.Stage;
            var index = Array.FindIndex(RouteStep.StageOrder, x =>
                string.Equals(x, stage, StringComparison.OrdinalIgnoreCase));
            if (message.Stage == "shaders")
            {
                HeadlineText.Text = "Preparing graphics.";
                StatusText.Text = "PREPARING GRAPHICS";
                SetPrimaryText("PREPARING…");
            }
            else if (message.Stage == "play" && _gameExecutable is not null)
            {
                HeadlineText.Text = "Controller A, Space, or left click.";
                SubheadText.Text = "Selects the highlighted menu item; Enter is Start. Press F6 in game for settings.";
                StatusText.Text = "GAME RUNNING";
                SetPrimaryText("GAME RUNNING");
            }
            if (index >= 0)
            {
                for (var i = 0; i < _steps.Count; i++)
                    _steps[i].SetState(i < index ? StepState.Complete : i == index ? StepState.Active : StepState.Waiting,
                        WaitingBrush, ActiveBrush, CompleteBrush, FailedBrush);
            }
            if (message.Percent is >= 0 and <= 100)
                SetProgress(message.Percent, message.Message);
            else if (!string.IsNullOrWhiteSpace(message.Message))
                ProgressMessageText.Text = message.Message;
            if (!string.IsNullOrWhiteSpace(message.Message))
                AppendLog(message.Message);
        }
        catch (JsonException)
        {
            AppendLog(line);
        }
    }

    private void SetProgress(int percent, string? message)
    {
        BuildProgress.Value = percent;
        ProgressText.Text = $"{percent}%";
        if (!string.IsNullOrWhiteSpace(message))
            ProgressMessageText.Text = message;
    }

    // What the next start uses, read straight from the settings file.
    private string ConfiguredGraphicsApi()
    {
        if (_stateRoot is null) return "vulkan";
        var config = Path.Combine(_stateRoot, "config", "pinyon_shift.toml");
        if (!File.Exists(config)) return "vulkan";
        try
        {
            var text = File.ReadAllText(config);
            var schema = Regex.Match(text, @"(?m)^\s*pinyon_shift_config_schema\s*=\s*([0-9]+)");
            var backend = Regex.Match(text, @"(?m)^\s*gpu_backend\s*=\s*""([^""]*)""");
            // Config schema 27 moves earlier files to Vulkan when the game starts;
            // "any" is the first backend, Direct3D 12.
            if (!schema.Success || int.Parse(schema.Groups[1].Value) < 27 || !backend.Success) return "vulkan";
            return string.Equals(backend.Groups[1].Value, "vulkan", StringComparison.OrdinalIgnoreCase) ? "vulkan" : "d3d12";
        }
        catch (IOException) { return "vulkan"; }
    }

    private int ConfiguredResolutionScale()
    {
        if (_stateRoot is null) return 1;
        var config = Path.Combine(_stateRoot, "config", "pinyon_shift.toml");
        try
        {
            var match = File.Exists(config)
                ? Regex.Match(File.ReadAllText(config), @"(?m)^\s*draw_resolution_scale_x\s*=\s*([0-9]+)")
                : Match.Empty;
            return match.Success ? Math.Clamp(int.Parse(match.Groups[1].Value), 1, 4) : 1;
        }
        catch (IOException) { return 1; }
    }

    private void UpdateReadyTiles()
    {
        var vulkan = ConfiguredGraphicsApi() == "vulkan";
        ApiTileText.Text = vulkan ? "Vulkan" : "Direct3D 12";
        ApiTileDetail.Text = vulkan
            ? "Recorded on a second thread; 120 fps at 1×."
            : "Prebuilt shader packs, one commands thread.";
        var scale = ConfiguredResolutionScale();
        ResolutionTileText.Text = $"{scale}×";
        ResolutionTileDetail.Text = $"{1280 * scale} × {720 * scale}";
    }

    private void DetectExistingBuild()
    {
        _gameExecutable = null;
        if (_repositoryRoot is null) return;
        var candidate = Path.Combine(_repositoryRoot, "out", "build", "win-amd64-release", "pinyon_shift.exe");
        if (File.Exists(candidate))
        {
            if (!File.Exists(Path.Combine(_repositoryRoot, ".local", "game", "base", "default.xex")))
            {
                HeadlineText.Text = "Restore your local game files.";
                SubheadText.Text = "Choose your disc image and run setup again. Your save stays in place.";
                StatusText.Text = "GAME FILES MISSING";
                AppendLog("Select your disc image and run setup to restore the missing game files. Your save stays in place.");
                return;
            }
            var payloadMarker = Path.Combine(_repositoryRoot, ".pinyon-source-sha256");
            if (File.Exists(payloadMarker))
            {
                var matchesRelease = false;
                try
                {
                    using var build = JsonDocument.Parse(File.ReadAllText(Path.Combine(_repositoryRoot, ".local", "build.json")));
                    matchesRelease = build.RootElement.TryGetProperty("pinyon_shift_source_payload_sha256", out var hash)
                        && string.Equals(hash.GetString(), File.ReadAllText(payloadMarker).Trim(), StringComparison.OrdinalIgnoreCase);
                }
                catch (Exception ex) when (ex is IOException or JsonException or InvalidOperationException) { }
                if (!matchesRelease)
                {
                    HeadlineText.Text = "Update your local build.";
                    SubheadText.Text = "This release changed the game code. Choose your disc image and run setup; game files and your save are kept.";
                    StatusText.Text = "BUILD UPDATE NEEDED";
                    AppendLog("Select your disc image and run setup to build this release. Existing game files and your save are preserved.");
                    return;
                }
            }
            _gameExecutable = candidate;
            SetComplete();
            // Only Direct3D 12 loads prepared shader packs.
            if (_stateRoot is not null && ConfiguredGraphicsApi() == "d3d12" &&
                !File.Exists(Path.Combine(_stateRoot, "cache", "fh1-artifacts.json")))
            {
                _steps[3].SetState(StepState.Waiting, WaitingBrush, ActiveBrush, CompleteBrush, FailedBrush);
                _steps[4].SetState(StepState.Waiting, WaitingBrush, ActiveBrush, CompleteBrush, FailedBrush);
                HeadlineText.Text = "Finish preparing your preview.";
                SubheadText.Text = "Direct3D 12 prepares its shaders once for this PC before the first start.";
                StatusText.Text = "GRAPHICS PREPARATION NEEDED";
                SetPrimaryText("PREPARE & PLAY");
            }
        }
    }

    private async Task LaunchGameAsync()
    {
        if (_repositoryRoot is null || _stateRoot is null || _gameExecutable is null) return;
        if (_busy) return;

        _busy = true;
        ChooseInstallRootButton.IsEnabled = false;
        GraphicsSettingsButton.IsEnabled = false;
        PrimaryButton.IsEnabled = false;
        SetPrimaryText("STARTING…");
        EyebrowText.Text = "STARTING";
        HeadlineText.Text = "Warming up the engine.";
        SubheadText.Text = "The game opens in its own window. This screen reports back when it closes.";
        StatusText.Text = "STARTING";
        ShowPanel(View.Log);
        SetProgress(0, "Checking graphics for this computer.");
        StatusDot.Fill = ActiveBrush;
        ReportProblemButton.IsEnabled = false;
        AppendLog("Checking graphics for this computer. Missing or outdated shaders are prepared automatically.");
        AppendLog("Controls: use controller A, Space, or left click for the selected Xbox menu item; press Enter for Start.");

        try
        {
            _cancellation?.Dispose();
            _cancellation = new CancellationTokenSource();
            var launcher = Path.Combine(_repositoryRoot, "tools", "launch-preview.ps1");
            var startInfo = new ProcessStartInfo
            {
                FileName = "powershell.exe",
                WorkingDirectory = _repositoryRoot,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true
            };
            foreach (var argument in new[]
            {
                "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", launcher,
                "-Configuration", "Release", "-StateRoot", _stateRoot, "-Json", "-JsonEvents"
            }) startInfo.ArgumentList.Add(argument);

            using var watcher = new Process { StartInfo = startInfo };
            var output = new System.Text.StringBuilder();
            var errors = new System.Text.StringBuilder();
            watcher.OutputDataReceived += (_, args) => Dispatcher.Invoke(() =>
            {
                if (args.Data is null) return;
                if (args.Data.StartsWith('{')) output.AppendLine(args.Data);
                else HandleOutput(args.Data);
            });
            watcher.ErrorDataReceived += (_, args) => Dispatcher.Invoke(() =>
            {
                if (args.Data is null) return;
                errors.AppendLine(args.Data);
                AppendLog(args.Data);
            });
            if (!watcher.Start()) throw new InvalidOperationException("Windows could not start the preview watcher.");
            watcher.BeginOutputReadLine();
            watcher.BeginErrorReadLine();
            using var registration = _cancellation.Token.Register(() =>
            {
                try { if (!watcher.HasExited) watcher.Kill(entireProcessTree: true); } catch { }
            });
            await watcher.WaitForExitAsync(_cancellation.Token);
            var error = errors.ToString();

            var result = ParseLaunchResult(output.ToString());
            if (watcher.ExitCode == 0 && string.Equals(result?.Result, "normal-exit", StringComparison.OrdinalIgnoreCase))
            {
                AppendLog("The game closed normally.");
                SetComplete();
                return;
            }

            DetectPendingReport();
            if (_pendingReport is null && result is not null &&
                !string.IsNullOrWhiteSpace(result.CrashId) &&
                !string.IsNullOrWhiteSpace(result.Bundle) &&
                !string.IsNullOrWhiteSpace(result.IssueUrl))
            {
                SetPendingReport(new CrashReport(result.CrashId, result.Bundle, result.IssueUrl,
                    $"0x{unchecked((uint)result.ExitCode):X8}"));
            }
            if (_pendingReport is null)
                throw new InvalidOperationException(string.IsNullOrWhiteSpace(error)
                    ? "The game exited unexpectedly, but its diagnostic report could not be prepared."
                    : error.Trim());
        }
        catch (OperationCanceledException)
        {
            SetFailure("PREPARATION CANCELLED", "Run the launcher again to finish preparing graphics.");
        }
        catch (Exception ex)
        {
            SetFailure("PREVIEW STOPPED", ex.Message);
        }
        finally
        {
            _busy = false;
            GraphicsSettingsButton.IsEnabled = true;
            ReportProblemButton.IsEnabled = true;
            UpdatePrimaryButton();
        }
    }

    private static LaunchResult? ParseLaunchResult(string output)
    {
        foreach (var line in output.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries).Reverse())
        {
            try
            {
                var result = JsonSerializer.Deserialize<LaunchResult>(line, new JsonSerializerOptions
                {
                    PropertyNameCaseInsensitive = true
                });
                if (result is not null) return result;
            }
            catch (JsonException) { }
        }
        return null;
    }

    private void DetectPendingReport()
    {
        if (_stateRoot is null) return;
        var reportsRoot = Path.GetFullPath(Path.Combine(_stateRoot, "reports"));
        var marker = Path.Combine(reportsRoot, "pending-report.json");
        if (!File.Exists(marker)) return;
        try
        {
            var report = JsonSerializer.Deserialize<CrashReport>(File.ReadAllText(marker), new JsonSerializerOptions
            {
                PropertyNameCaseInsensitive = true
            });
            if (report is null || string.IsNullOrWhiteSpace(report.CrashId) ||
                string.IsNullOrWhiteSpace(report.Bundle) || string.IsNullOrWhiteSpace(report.IssueUrl)) return;
            var bundle = Path.GetFullPath(report.Bundle);
            var reportsPrefix = reportsRoot.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            if (!bundle.StartsWith(reportsPrefix, StringComparison.OrdinalIgnoreCase) || !File.Exists(bundle)) return;
            if (!Uri.TryCreate(report.IssueUrl, UriKind.Absolute, out var issueUri) ||
                issueUri.Scheme != Uri.UriSchemeHttps || issueUri.Host != "github.com" ||
                !issueUri.AbsolutePath.StartsWith("/arcanite24/pinyon-shift/issues/new", StringComparison.OrdinalIgnoreCase)) return;
            SetPendingReport(report with { Bundle = bundle, IssueUrl = issueUri.AbsoluteUri });
        }
        catch (IOException) { }
        catch (JsonException) { }
    }

    private void SetPendingReport(CrashReport report)
    {
        _pendingReport = report;
        for (var i = 0; i < _steps.Count; i++)
            _steps[i].SetState(i == _steps.Count - 1 ? StepState.Failed : StepState.Complete,
                WaitingBrush, ActiveBrush, CompleteBrush, FailedBrush);
        EyebrowText.Text = "CRASH REPORT READY";
        HeadlineText.Text = "We caught the crash.";
        SubheadText.Text = "Reporting it takes one click and helps fix it for everyone.";
        StatusText.Text = "REPORT READY";
        StatusDot.Fill = FailedBrush;
        CrashIdText.Text = report.CrashId;
        ShowPanel(View.Crash);
        ReportProblemButton.Visibility = Visibility.Collapsed;
        OpenLogsButton.Content = "OPEN REPORT FOLDER";
        OpenLogsButton.Visibility = Visibility.Visible;
        SetPrimaryText("REPORT CRASH");
        PrimaryButton.IsEnabled = true;
    }

    private void ReportCrash()
    {
        if (_pendingReport is null || _stateRoot is null) return;
        Process.Start(new ProcessStartInfo
        {
            FileName = "explorer.exe",
            UseShellExecute = true,
            Arguments = $"/select,\"{_pendingReport.Bundle}\""
        });
        Process.Start(new ProcessStartInfo(_pendingReport.IssueUrl) { UseShellExecute = true });
        var marker = Path.Combine(_stateRoot, "reports", "pending-report.json");
        try { if (File.Exists(marker)) File.Delete(marker); } catch (IOException) { }
        StatusText.Text = "GITHUB OPENED";
        SetPrimaryText("OPEN GITHUB AGAIN");
    }

    private async Task<string> ResolveRepositoryRootAsync(string? selectedInstallRoot = null)
    {
        static bool IsRoot(string path) => File.Exists(Path.Combine(path, "config", "supported-dumps.json"))
            && File.Exists(Path.Combine(path, "tools", "setup-preview.ps1"));

        var directory = AppContext.BaseDirectory;
        for (var i = 0; i < 8; i++)
        {
            if (IsRoot(directory))
            {
                _canChooseInstallRoot = false;
                return directory;
            }
            var parent = Directory.GetParent(directory);
            if (parent is null) break;
            directory = parent.FullName;
        }

        var payload = Path.Combine(AppContext.BaseDirectory, "pinyon-shift-source.zip");
        if (!File.Exists(payload))
            throw new FileNotFoundException("Keep pinyon-shift-source.zip beside the launcher, or run the launcher from a repository checkout.");

        var version = typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "dev";
        var installRoot = Environment.GetEnvironmentVariable("PINYON_SHIFT_INSTALL_ROOT");
        _canChooseInstallRoot = string.IsNullOrWhiteSpace(installRoot);
        if (_canChooseInstallRoot)
            installRoot = selectedInstallRoot ?? (File.Exists(InstallRootPreference)
                ? (await File.ReadAllTextAsync(InstallRootPreference)).Trim() : null);
        if (string.IsNullOrWhiteSpace(installRoot))
            installRoot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PinyonShift");
        var destination = Path.Combine(Path.GetFullPath(installRoot), "source", version);
        var payloadHash = await Task.Run(async () =>
        {
            await using var stream = File.OpenRead(payload);
            return Convert.ToHexString(await SHA256.HashDataAsync(stream));
        });
        var payloadMarker = Path.Combine(destination, ".pinyon-source-sha256");
        var installedHash = File.Exists(payloadMarker)
            ? (await File.ReadAllTextAsync(payloadMarker)).Trim()
            : string.Empty;
        if (!IsRoot(destination) || !string.Equals(installedHash, payloadHash,
                StringComparison.OrdinalIgnoreCase))
        {
            Directory.CreateDirectory(destination);
            await Task.Run(() => ZipFile.ExtractToDirectory(payload, destination, overwriteFiles: true));
            await File.WriteAllTextAsync(payloadMarker, payloadHash + Environment.NewLine);
        }
        if (!IsRoot(destination))
            throw new InvalidDataException("The release source payload is incomplete.");
        return destination;
    }

    private static string ResolveStateRoot(string repositoryRoot)
    {
        var configured = Environment.GetEnvironmentVariable("PINYON_SHIFT_STATE_ROOT");
        return Path.GetFullPath(string.IsNullOrWhiteSpace(configured)
            ? Path.Combine(repositoryRoot, ".local", "preview")
            : configured);
    }

    private void StageControllerMappings()
    {
        if (_repositoryRoot is null) return;
        var source = Path.Combine(_repositoryRoot, "config", "gamecontrollerdb.txt");
        var executable = Path.Combine(_repositoryRoot, "out", "build", "win-amd64-release",
            "pinyon_shift.exe");
        if (!File.Exists(source) || !File.Exists(executable)) return;
        var destination = Path.Combine(Path.GetDirectoryName(executable)!, "gamecontrollerdb.txt");
        File.Copy(source, destination, overwrite: true);
        AppendLog("Controller compatibility mappings are current.");
    }

    private void ShowPanel(View panel)
    {
        _panel = panel;
        SetupPanel.Visibility = panel == View.Setup ? Visibility.Visible : Visibility.Collapsed;
        ReadyPanel.Visibility = panel == View.Ready ? Visibility.Visible : Visibility.Collapsed;
        LogPanel.Visibility = panel == View.Log ? Visibility.Visible : Visibility.Collapsed;
        CrashPanel.Visibility = panel == View.Crash ? Visibility.Visible : Visibility.Collapsed;
        GraphicsPanel.Visibility = panel == View.Graphics ? Visibility.Visible : Visibility.Collapsed;
        // The route only tells something while there is setup left to do.
        RouteList.Visibility = panel is View.Ready or View.Graphics ? Visibility.Collapsed : Visibility.Visible;
        if (panel == View.Ready) UpdateReadyTiles();
    }

    private void SetPrimaryText(string text)
    {
        PrimaryButtonText.Text = text;
        PrimaryIcon.Visibility = text.StartsWith("PLAY", StringComparison.Ordinal) ||
                                 text.StartsWith("PREPARE & PLAY", StringComparison.Ordinal)
            ? Visibility.Visible : Visibility.Collapsed;
    }

    private void SetReadyState()
    {
        EyebrowText.Text = "READY FOR YOUR DISC";
        HeadlineText.Text = "Build your preview.";
        SubheadText.Text = "Your game stays yours. The launcher verifies your disc image, builds the native translation on this PC, and keeps every generated file local.";
        StatusText.Text = "SYSTEM READY";
        StatusDot.Fill = CompleteBrush;
    }

    private void SetComplete()
    {
        _pendingReport = null;
        foreach (var step in _steps)
            step.SetState(StepState.Complete, WaitingBrush, ActiveBrush, CompleteBrush, FailedBrush);
        SetProgress(100, "Ready.");
        EyebrowText.Text = "LOCAL BUILD COMPLETE";
        HeadlineText.Text = "The road is open.";
        SubheadText.Text = "Everything was built on this PC from your own disc. Your saves live next to the build and are never uploaded.";
        StatusText.Text = "READY TO PLAY";
        StatusDot.Fill = CompleteBrush;
        SetPrimaryText("PLAY PINYON SHIFT");
        ShowPanel(View.Ready);
        ReportProblemButton.Visibility = Visibility.Visible;
        OpenLogsButton.Content = "OPEN LOGS";
        OpenLogsButton.Visibility = Visibility.Visible;
        AppendLog("Build complete. Generated files remain on this computer.");
    }

    private void SetFailure(string eyebrow, string message)
    {
        var active = _steps.FirstOrDefault(x => x.State == StepState.Active);
        active?.SetState(StepState.Failed, WaitingBrush, ActiveBrush, CompleteBrush, FailedBrush);
        EyebrowText.Text = eyebrow;
        HeadlineText.Text = "We stopped safely.";
        SubheadText.Text = message;
        StatusText.Text = "ACTION NEEDED";
        StatusDot.Fill = FailedBrush;
        SetPrimaryText("TRY AGAIN");
        ShowPanel(View.Log);
        ProgressMessageText.Text = message;
        OpenLogsButton.Visibility = Visibility.Visible;
        AppendLog($"ERROR: {message}");
    }

    private void ResetRoute()
    {
        _gameExecutable = null;
        _pendingReport = null;
        foreach (var step in _steps)
            step.SetState(StepState.Waiting, WaitingBrush, ActiveBrush, CompleteBrush, FailedBrush);
        SetPrimaryText("VERIFY & BUILD");
        ShowPanel(View.Setup);
        ReportProblemButton.Visibility = Visibility.Visible;
        OwnershipCheckBox.Visibility = Visibility.Visible;
    }

    private void UpdatePrimaryButton()
    {
        ChooseInstallRootButton.IsEnabled = !_busy && _canChooseInstallRoot &&
            GraphicsPanel.Visibility != Visibility.Visible;
        PrimaryButton.IsEnabled = !_busy && (_pendingReport is not null || _gameExecutable is not null ||
            (_repositoryRoot is not null && File.Exists(IsoPathTextBox.Text) && OwnershipCheckBox.IsChecked == true));
    }

    private void AppendLog(string line)
    {
        var entry = $"[{DateTime.Now:HH:mm:ss}] {line}";
        LogTextBox.AppendText(entry + Environment.NewLine);
        try
        {
            _sessionLog?.WriteLine(entry);
        }
        catch (IOException)
        {
            _sessionLog?.Dispose();
            _sessionLog = null;
        }
        const int maximumLogCharacters = 120_000;
        if (LogTextBox.Text.Length > maximumLogCharacters)
            LogTextBox.Text = LogTextBox.Text[^maximumLogCharacters..];
        LogTextBox.ScrollToEnd();
    }

    private void LogToggleButton_Click(object sender, RoutedEventArgs e)
    {
        var show = LogBox.Visibility != Visibility.Visible;
        LogBox.Visibility = show ? Visibility.Visible : Visibility.Collapsed;
        LogToggleButton.Content = show ? "HIDE" : "SHOW";
    }

    private void OpenLogsButton_Click(object sender, RoutedEventArgs e)
    {
        if (_repositoryRoot is null || _stateRoot is null) return;
        if (_pendingReport is not null)
        {
            Process.Start(new ProcessStartInfo
            {
                FileName = "explorer.exe",
                UseShellExecute = true,
                Arguments = $"/select,\"{_pendingReport.Bundle}\""
            });
            return;
        }
        var logs = Path.Combine(_repositoryRoot, ".local", "logs");
        Directory.CreateDirectory(logs);
        Process.Start(new ProcessStartInfo("explorer.exe", logs) { UseShellExecute = true });
    }

    private void StartSessionLog(string repositoryRoot)
    {
        try
        {
            var logs = Path.Combine(repositoryRoot, ".local", "logs");
            Directory.CreateDirectory(logs);
            _sessionLog = new StreamWriter(Path.Combine(logs, "launcher.log"), append: false)
            {
                AutoFlush = true
            };
        }
        catch (IOException)
        {
            _sessionLog = null;
        }
        catch (UnauthorizedAccessException)
        {
            _sessionLog = null;
        }
    }

    private void ReportProblemButton_Click(object sender, RoutedEventArgs e) =>
        Process.Start(new ProcessStartInfo(
            "https://github.com/arcanite24/pinyon-shift/issues/new?template=bug.yml")
        { UseShellExecute = true });

    private void ProjectButton_Click(object sender, RoutedEventArgs e) =>
        Process.Start(new ProcessStartInfo("https://github.com/arcanite24/pinyon-shift") { UseShellExecute = true });

    private void OpenStateFolderButton_Click(object sender, RoutedEventArgs e)
    {
        if (_stateRoot is null) return;
        Directory.CreateDirectory(_stateRoot);
        Process.Start(new ProcessStartInfo("explorer.exe", _stateRoot) { UseShellExecute = true });
    }

    private void MinimizeButton_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void MaximizeButton_Click(object sender, RoutedEventArgs e) =>
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

    private void CloseButton_Click(object sender, RoutedEventArgs e) => Close();

    private async void GraphicsSettingsButton_Click(object sender, RoutedEventArgs e)
    {
        if (_repositoryRoot is null || _busy || _pendingReport is not null) return;
        if (_panel != View.Graphics) _panelBeforeGraphics = _panel;
        ShowPanel(View.Graphics);
        ChooseInstallRootButton.IsEnabled = false;
        GraphicsStatusText.Text = "Loading current settings…";
        try
        {
            ApplyGraphicsResult(await RunGraphicsSettingsToolAsync("Get"));
            GraphicsStatusText.Text = "Current settings loaded. Saving a change applies at the next start.";
        }
        catch (Exception ex)
        {
            GraphicsStatusText.Text = $"Settings could not be loaded: {ex.Message}";
        }
    }

    private void CloseGraphicsButton_Click(object sender, RoutedEventArgs e)
    {
        ShowPanel(_panelBeforeGraphics == View.Graphics ? View.Setup : _panelBeforeGraphics);
        UpdatePrimaryButton();
    }

    private async void SaveGraphicsButton_Click(object sender, RoutedEventArgs e)
    {
        await ChangeGraphicsSettingsAsync("Apply", "Settings saved. They apply at the next start.");
        // Direct3D 12 may now need its shader packs.
        if (_gameExecutable is not null && !_busy) DetectExistingBuild();
    }

    private void InGameSettingsButton_Click(object sender, RoutedEventArgs e) =>
        MessageBox.Show(this,
            "Display, graphics, audio and control settings live in the game now. Press F6 while " +
            "playing to open them. Changes there apply at once, except the resolution scale and the " +
            "graphics API, and are saved to the same settings file this launcher uses, with a backup " +
            "before the first change of each session.",
            "In-game settings", MessageBoxButton.OK, MessageBoxImage.Information);

    private async void ResetGraphicsButton_Click(object sender, RoutedEventArgs e)
    {
        if (MessageBox.Show(this,
                "Reset only the Pinyon Shift runtime settings? Your current pinyon_shift.toml will be backed up first.",
                "Reset runtime settings", MessageBoxButton.OKCancel, MessageBoxImage.Warning) != MessageBoxResult.OK)
            return;
        await ChangeGraphicsSettingsAsync("Reset", "Runtime settings reset. They apply at the next start.",
            revealBackup: true);
    }

    private async void RestoreGraphicsButton_Click(object sender, RoutedEventArgs e) =>
        await ChangeGraphicsSettingsAsync("Restore", "Latest settings backup restored. It applies at the next start.");

    private async Task ChangeGraphicsSettingsAsync(string action, string success, bool revealBackup = false)
    {
        SetGraphicsControlsEnabled(false);
        GraphicsStatusText.Text = action == "Apply" ? "Saving validated settings…" : "Updating runtime settings…";
        try
        {
            var result = await RunGraphicsSettingsToolAsync(action);
            ApplyGraphicsResult(result);
            GraphicsStatusText.Text = success;
            if (revealBackup && !string.IsNullOrWhiteSpace(result.BackupPath) && File.Exists(result.BackupPath))
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = "explorer.exe",
                    UseShellExecute = true,
                    Arguments = $"/select,\"{result.BackupPath}\""
                });
            }
        }
        catch (Exception ex)
        {
            GraphicsStatusText.Text = $"No settings were changed: {ex.Message}";
        }
        finally
        {
            SetGraphicsControlsEnabled(true);
        }
    }

    private async Task<GraphicsResult> RunGraphicsSettingsToolAsync(string action)
    {
        if (_repositoryRoot is null || _stateRoot is null)
            throw new InvalidOperationException("Release source is not ready.");
        var script = Path.Combine(_repositoryRoot, "tools", "set-graphics-experiment.ps1");
        if (!File.Exists(script)) throw new FileNotFoundException("The graphics settings tool is missing.", script);
        var startInfo = new ProcessStartInfo
        {
            FileName = "powershell.exe",
            WorkingDirectory = _repositoryRoot,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        foreach (var argument in new[]
        {
            "-NoLogo", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", script,
            "-Action", action, "-StateRoot", _stateRoot,
            // Only the scale and the API: the rest is set in game and must not
            // be overwritten.
            "-ResolutionScale", SelectedTag(ResolutionComboBox),
            "-GraphicsApi", SelectedTag(GraphicsApiComboBox),
            "-Json"
        }) startInfo.ArgumentList.Add(argument);
        using var process = Process.Start(startInfo) ??
            throw new InvalidOperationException("Windows could not start the graphics settings tool.");
        var outputTask = process.StandardOutput.ReadToEndAsync();
        var errorTask = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        var output = await outputTask;
        var error = await errorTask;
        if (process.ExitCode != 0)
            throw new InvalidOperationException(string.IsNullOrWhiteSpace(error) ? "The settings tool stopped." : error.Trim());
        var result = JsonSerializer.Deserialize<GraphicsResult>(output.Trim(), new JsonSerializerOptions
        {
            PropertyNameCaseInsensitive = true
        });
        return result ?? throw new InvalidDataException("The settings tool returned an invalid result.");
    }

    private static string SelectedTag(ComboBox comboBox) =>
        (comboBox.SelectedItem as ComboBoxItem)?.Tag?.ToString() ?? throw new InvalidOperationException("Choose a setting first.");

    private void ApplyGraphicsResult(GraphicsResult result)
    {
        SelectTag(ResolutionComboBox, result.Settings.ResolutionScale.ToString());
        SelectTag(GraphicsApiComboBox, string.IsNullOrWhiteSpace(result.Settings.GraphicsApi)
            ? "vulkan" : result.Settings.GraphicsApi);
        UpdateReadyTiles();
    }

    private static void SelectTag(ComboBox comboBox, string value)
    {
        comboBox.SelectedItem = comboBox.Items.OfType<ComboBoxItem>()
            .FirstOrDefault(item => string.Equals(item.Tag?.ToString(), value, StringComparison.OrdinalIgnoreCase));
    }

    private void SetGraphicsControlsEnabled(bool enabled)
    {
        ResolutionComboBox.IsEnabled = enabled;
        GraphicsApiComboBox.IsEnabled = enabled;
        SaveGraphicsButton.IsEnabled = enabled;
        ResetGraphicsButton.IsEnabled = enabled;
        RestoreGraphicsButton.IsEnabled = enabled;
    }

    private sealed record ProgressMessage(string? Stage, int Percent, string? Message);
    private sealed record LaunchResult(
        [property: JsonPropertyName("result")] string? Result,
        [property: JsonPropertyName("crash_id")] string? CrashId,
        [property: JsonPropertyName("bundle")] string? Bundle,
        [property: JsonPropertyName("issue_url")] string? IssueUrl,
        [property: JsonPropertyName("exit_code")] long ExitCode);
    private sealed record CrashReport(
        [property: JsonPropertyName("crash_id")] string CrashId,
        [property: JsonPropertyName("bundle")] string Bundle,
        [property: JsonPropertyName("issue_url")] string IssueUrl,
        [property: JsonPropertyName("exit_code_hex")] string? ExitCodeHex);
    private sealed record GraphicsResult(
        [property: JsonPropertyName("backup_path")] string? BackupPath,
        [property: JsonPropertyName("settings")] GraphicsSettings Settings,
        [property: JsonPropertyName("restart_required")] bool RestartRequired);
    private sealed record GraphicsSettings(
        [property: JsonPropertyName("anisotropy")] int Anisotropy,
        [property: JsonPropertyName("post_effect")] string PostEffect,
        [property: JsonPropertyName("disable_motion_blur")] bool DisableMotionBlur,
        [property: JsonPropertyName("disable_depth_of_field")] bool DisableDepthOfField,
        [property: JsonPropertyName("preset")] string Preset,
        [property: JsonPropertyName("resolution_scale")] int ResolutionScale,
        [property: JsonPropertyName("graphics_api")] string? GraphicsApi,
        [property: JsonPropertyName("clear_memory_page_state")] bool ClearMemoryPageState,
        [property: JsonPropertyName("vsync")] bool Vsync);
}
