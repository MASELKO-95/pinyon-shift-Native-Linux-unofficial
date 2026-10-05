using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Media.Imaging;
using Avalonia.Platform.Storage;
using Avalonia.Threading;

namespace PinyonShift.Launcher.Linux;

public partial class MainWindow : Window
{
    private readonly LauncherBackend _backend;
    private readonly ConcurrentQueue<string> _lines = new();
    private readonly DispatcherTimer _timer;
    private readonly DispatcherTimer _stateTimer;
    private readonly FileStream _instanceLock;
    private CancellationTokenSource? _cancellation;
    private bool _initialized, _busy, _playing, _chooseIso;
    private static readonly string[] ScalingNames = ["bilinear", "cas", "fsr"];

    public MainWindow() : this(Program.Options) { }

    public MainWindow(LauncherOptions options)
    {
        _backend = new(options);
        string local = Path.Combine(options.Repository, ".local");
        Directory.CreateDirectory(local);
        try { _instanceLock = new(Path.Combine(local, "linux-launcher.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
        catch (IOException) { throw new IOException("A launcher is already open for this project."); }
        InitializeComponent();
        DiscEditionChoice.ItemsSource = _backend.DiscEditions();
        DiscEditionChoice.SelectedIndex = 0;
        GameLanguageChoice.ItemsSource = _backend.GameLanguages();
        GameLanguageChoice.SelectedIndex = 0;
        VersionText.Text = $"Pinyon Shift {_backend.Version}";
        if (_backend.Distribution is { } distribution)
        {
            PortCredit.Text = $"Unofficial Linux port by {distribution.Maintainer}";
            VersionsButton.IsVisible = true;
        }
        InstallLocation.Text = $"Build: {options.Repository}\nSaves and settings: {options.State}";
        DetectInstalledEdition();
        IsoPath.Text = _backend.SavedIsoPath();
        _initialized = true;
        _timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(120) };
        _timer.Tick += (_, _) => DrainLog();
        _timer.Start();
        _stateTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        _stateTimer.Tick += (_, _) => RefreshState();
        _stateTimer.Start();
        AddHandler(DragDrop.DragOverEvent, OnDragOver);
        AddHandler(DragDrop.DropEvent, OnDrop);
        Opened += OnOpened;
        Closing += (_, e) =>
        {
            if (!_busy) return;
            e.Cancel = true;
            Notice(_playing ? "Close the game window before closing the launcher."
                : "Wait for this operation to finish, or cancel the build before closing.");
        };
        Closed += (_, _) => { _timer.Stop(); _stateTimer.Stop(); _instanceLock.Dispose(); };
        RefreshState();
    }

    private async void OnOpened(object? sender, EventArgs e)
    {
        try
        {
            await RefreshSummary();
            if (_backend.PendingReport() is not null)
                Notice("A local diagnostic report from the last failed launch is available. Nothing has been uploaded.", true);
            if (_backend.Options.ShowSettings) await ShowSettings();
            if (_backend.Options.Screenshot is { } path)
            {
                await Task.Delay(600);
                using var bitmap = new RenderTargetBitmap(new PixelSize((int)Bounds.Width, (int)Bounds.Height));
                bitmap.Render(this);
                bitmap.Save(path);
                Close();
            }
        }
        catch (Exception error)
        {
            Notice(error.Message);
            if (_backend.Options.Screenshot is not null)
            {
                Console.Error.WriteLine(error.Message);
                if (Application.Current?.ApplicationLifetime is Avalonia.Controls.ApplicationLifetimes.IClassicDesktopStyleApplicationLifetime desktop)
                    desktop.Shutdown(1);
            }
        }
    }

    private void DetectInstalledEdition()
    {
        try
        {
            using var build = JsonDocument.Parse(File.ReadAllText(Path.Combine(_backend.Options.Repository, ".local/build.json")));
            string? hash = build.RootElement.GetProperty("guest_executable_sha256").GetString();
            DiscEditionChoice.SelectedItem = DiscEditionChoice.Items.Cast<DiscEdition>().FirstOrDefault(
                edition => string.Equals(edition.ExecutableHash, hash, StringComparison.OrdinalIgnoreCase))
                ?? DiscEditionChoice.Items[0];
        }
        catch (Exception e) when (e is IOException or JsonException or KeyNotFoundException) { }
    }

    private void RefreshState()
    {
        if (!_initialized) return;
        bool ready = _backend.IsReady && !_chooseIso;
        bool gameRunning = _playing || LauncherBackend.GameRunning();
        SetupPanel.IsVisible = !ready && !_busy && !SettingsPanel.IsVisible;
        ReadyPanel.IsVisible = ready && !_busy && !SettingsPanel.IsVisible;
        SettingsButton.IsEnabled = !_busy && !gameRunning;
        InstallDlcButton.IsEnabled = ready && !_busy && !gameRunning;
        ControlsChoice.IsEnabled = GameLanguageChoice.IsEnabled = TrainerEnabled.IsEnabled = DiscEditionChoice.IsEnabled = !_busy && !gameRunning;
        SaveSettingsButton.IsEnabled = ResetSettingsButton.IsEnabled =
            RestoreSettingsButton.IsEnabled = !_busy && !gameRunning;
        RebuildButton.IsVisible = _backend.HasGame && !_chooseIso;
        RebuildButton.IsEnabled = !_busy && !gameRunning;
        SelectIsoButton.IsVisible = _backend.IsReady || _chooseIso;
        SelectIsoButton.Content = _chooseIso ? "Back" : "Choose another ISO";
        SelectIsoButton.IsEnabled = !_busy && !gameRunning;
        PrimaryButton.IsEnabled = !_busy && !gameRunning && !SettingsPanel.IsVisible &&
            (ready || (Ownership.IsChecked == true && File.Exists(IsoPath.Text)));
        PrimaryButton.Content = _playing || gameRunning ? "Game running" : ready ? "▶  Play" : "Verify and build";
        if (!_busy)
        {
            Headline.Text = ready ? "Ready to drive" : "Build your preview";
            Subhead.Text = ready ? "Your native Linux preview is ready. Pick your settings and head out."
                : "Verified, extracted and compiled on this PC. Nothing is uploaded.";
        }
    }

    private async Task RefreshSummary()
    {
        DetectInstalledEdition();
        string? iso = _backend.SavedIsoPath();
        InstalledDisc.Text = $"Forza Horizon · {DiscEditionChoice.SelectedItem}\nISO: {iso ?? "Choose your disc image to record its location"}";
        var settings = await _backend.ReadSettingsAsync();
        ReadySummary.Text = $"Vulkan  ·  {settings.Scale}× internal resolution  ·  {settings.Scaling.ToUpperInvariant()} output scaling  ·  {(settings.RenderFps == 0 ? "Uncapped" : $"{settings.RenderFps} fps")}";
        var options = await _backend.ReadGameOptionsAsync();
        GameLanguageChoice.SelectedItem = GameLanguageChoice.Items.Cast<GameLanguage>().FirstOrDefault(
            language => language.Language == options.Language && language.Country == options.Country)
            ?? GameLanguageChoice.Items[0];
        TrainerEnabled.IsChecked = options.Trainer;
        ControlsChoice.SelectedIndex = 0;
    }

    private void Notice(string message, bool report = false)
    {
        NoticePanel.IsVisible = true;
        NoticeText.Text = message;
        ReportFolderButton.IsVisible = report;
    }

    private void DrainLog()
    {
        var batch = new System.Text.StringBuilder();
        // Bound each UI tick so a burst of game logs cannot monopolize the UI thread.
        for (int count = 0; count < 200 && _lines.TryDequeue(out string? line); count++)
        {
            var stage = StageEvent.Parse(line);
            if (stage is not null)
            {
                Progress.IsIndeterminate = false;
                Progress.Value = stage.Percent;
                PercentText.Text = $"{stage.Percent}%";
                ProgressMessage.Text = stage.Message;
            }
            else batch.AppendLine(line);
        }
        if (batch.Length == 0) return;
        string text = LogText.Text + batch;
        LogText.Text = text.Length > 32_000 ? text[^24_000..] : text;
        LogText.CaretIndex = LogText.Text.Length;
    }

    private async void InstallDlc_Click(object? sender, RoutedEventArgs e)
    {
        if (_busy || LauncherBackend.GameRunning()) return;
        var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Select your original FH1 DLC package (LIVE, PIRS or CON)",
            AllowMultiple = false
        });
        string? package = files.FirstOrDefault()?.TryGetLocalPath();
        if (package is null) return;
        await RunOperation("Installing original DLC", (log, token) =>
            _backend.ScriptAsync("pinyon-shift-cli.sh",
                ["dlc", "install", "--state-root", _backend.Options.State,
                 "--package", package, "--extractor",
                 Path.Combine(_backend.BuildDirectory, "pinyon_shift_dlc_extract")], log, token));
    }

    private async Task RunOperation(string headline,
        Func<Action<string>, CancellationToken, Task<ProcessResult>> operation, bool playing = false)
    {
        if (_busy) return;
        if (LauncherBackend.GameRunning()) { Notice("Close the running game before starting another operation."); return; }
        _busy = true;
        _playing = playing;
        // Reduce UI wakeups while the renderer is running in another process.
        _timer.Interval = TimeSpan.FromMilliseconds(playing ? 1000 : 120);
        SettingsPanel.IsVisible = false;
        NoticePanel.IsVisible = false;
        _cancellation = new();
        RefreshState();
        Headline.Text = headline;
        Subhead.Text = playing ? "Enjoy the drive. This window will be here when you return."
            : "You can follow the progress below. Your saves stay in their current location.";
        ProgressPanel.IsVisible = true;
        Progress.IsIndeterminate = true;
        Progress.Value = 0;
        PercentText.Text = "";
        ProgressMessage.Text = headline;
        CancelButton.IsVisible = !playing;
        CancelButton.IsEnabled = true;
        LogText.Text = "";
        try
        {
            string logs = Path.Combine(_backend.Options.Repository, ".local/logs");
            Directory.CreateDirectory(logs);
            using var log = new StreamWriter(Path.Combine(logs, $"launcher-{DateTime.UtcNow:yyyyMMddTHHmmssfff}-{Environment.ProcessId}.log"));
            var result = await operation(line => { log.WriteLine(line); _lines.Enqueue(line);
                // The full log is on disk; keep only a bounded tail for the launcher.
                while (_lines.Count > 2000) _lines.TryDequeue(out _); }, _cancellation.Token);
            DrainLog();
            Progress.IsIndeterminate = false;
            if (result.ExitCode != 0)
            {
                ProgressMessage.Text = $"Operation failed (exit {result.ExitCode})";
                Notice(playing ? "The game could not finish normally. See Details or Logs for the cause."
                    : "The operation failed. Open Details for the error; you can retry after correcting it.",
                    playing && _backend.PendingReport() is not null);
            }
            else
            {
                Progress.Value = 100;
                PercentText.Text = "100%";
                ProgressMessage.Text = playing ? "Game closed normally" : "Completed";
                _chooseIso = false;
            }
        }
        catch (OperationCanceledException)
        {
            ProgressMessage.Text = "Build cancelled";
            Notice("Build cancelled. Existing saves were not removed. You can run setup or rebuild again.");
        }
        catch (Exception error) { Notice(error.Message); ProgressMessage.Text = "Operation failed"; }
        finally
        {
            DrainLog();
            _cancellation.Dispose();
            _cancellation = null;
            _busy = _playing = false;
            _timer.Interval = TimeSpan.FromMilliseconds(120);
            CancelButton.IsVisible = false;
            Progress.IsIndeterminate = false;
            RefreshState();
            try { await RefreshSummary(); }
            catch (Exception error) { Notice(error.Message); }
        }
    }

    private async void Primary_Click(object? sender, RoutedEventArgs e)
    {
        if (_backend.IsReady && !_chooseIso)
        {
            var language = GameLanguageChoice.SelectedItem as GameLanguage;
            bool trainer = TrainerEnabled.IsChecked == true;
            string? controls = ControlsChoice.SelectedIndex switch
            {
                1 => "pc-camera", 2 => "pc-steering", 3 => "controller", _ => null
            };
            await RunOperation("Out on the road", async (log, token) =>
            {
                var previous = await _backend.ReadGameOptionsAsync();
                if (controls is not null || trainer != previous.Trainer || (language is { Id.Length: > 0 } &&
                    (language.Language != previous.Language || language.Country != previous.Country)))
                {
                    var options = new List<string> { "--action", "apply", "--state-root", _backend.Options.State,
                        "--trainer", trainer ? "true" : "false", "--json" };
                    if (language is { Id.Length: > 0 }) options.AddRange(["--game-language", language.Id]);
                    if (controls is not null) options.AddRange(["--controls", controls]);
                    var saved = await _backend.ScriptAsync("set-graphics-experiment.sh", options, log, token);
                    if (saved.ExitCode != 0) return saved;
                }
                return await _backend.ScriptAsync("launch-preview.sh",
                    ["--state-root", _backend.Options.State, "--json"], log, token);
            }, playing: true);
        }
        else
        {
            if (Ownership.IsChecked != true || !File.Exists(IsoPath.Text)) return;
            var args = new List<string> { "--iso-path", Path.GetFullPath(IsoPath.Text!), "--json-events" };
            if (DiscEditionChoice.SelectedItem is DiscEdition edition)
            {
                if (edition.Experimental) args.Add("--experimental");
                if (edition.Id.Length > 0) args.AddRange(["--dump-id", edition.Id]);
            }
            if (DownloadTools.IsChecked == true) args.Add("--download");
            await RunOperation("Building your preview", (log, token) =>
                _backend.ScriptAsync("setup-preview.sh", args, log, token));
        }
    }

    private async void Rebuild_Click(object? sender, RoutedEventArgs e)
    {
        DetectInstalledEdition();
        var args = new List<string> { "--json-events" };
        if (DiscEditionChoice.SelectedItem is DiscEdition { Experimental: true }) args.Add("--experimental");
        await RunOperation("Rebuilding your preview", async (log, token) =>
        {
            var prepare = await _backend.ScriptAsync("prepare-rexglue.sh", ["--json-events"], log, token);
            return prepare.ExitCode == 0
                ? await _backend.ScriptAsync("build-preview.sh", args, log, token) : prepare;
        });
    }

    private void Cancel_Click(object? sender, RoutedEventArgs e)
    {
        CancelButton.IsEnabled = false;
        ProgressMessage.Text = "Cancelling…";
        _cancellation?.Cancel();
    }
    private void IsoPath_Changed(object? sender, TextChangedEventArgs e) => RefreshState();
    private void Input_Changed(object? sender, RoutedEventArgs e) => RefreshState();
    private void SelectIso_Click(object? sender, RoutedEventArgs e)
    {
        _chooseIso = !_chooseIso;
        SettingsPanel.IsVisible = false;
        RefreshState();
    }
    private async void Browse_Click(object? sender, RoutedEventArgs e)
    {
        try
        {
            var files = await StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Choose your Forza Horizon disc image", AllowMultiple = false,
                FileTypeFilter = [new("Xbox 360 disc image") { Patterns = ["*.iso", "*.ISO"] }]
            });
            if (files.Count > 0 && files[0].TryGetLocalPath() is { } path) IsoPath.Text = path;
        }
        catch (Exception error) { Notice(error.Message); }
    }
    private static string? DroppedIso(DragEventArgs e) => e.DataTransfer.TryGetFiles()?
        .Select(file => file.TryGetLocalPath()).FirstOrDefault(path =>
            path is not null && Path.GetExtension(path).Equals(".iso", StringComparison.OrdinalIgnoreCase));
    private void OnDragOver(object? sender, DragEventArgs e) =>
        e.DragEffects = !_busy && DroppedIso(e) is not null ? DragDropEffects.Copy : DragDropEffects.None;
    private void OnDrop(object? sender, DragEventArgs e)
    {
        if (_busy || DroppedIso(e) is not { } path) return;
        _chooseIso = true;
        SettingsPanel.IsVisible = false;
        IsoPath.Text = path;
        RefreshState();
    }

    private async Task ShowSettings()
    {
        if (_busy || LauncherBackend.GameRunning()) { Notice("Close the game before changing settings."); return; }
        _busy = true;
        RefreshState();
        try
        {
            var settings = await _backend.ReadSettingsAsync();
            Resolution.SelectedIndex = settings.Scale - 1;
            Scaling.SelectedIndex = Math.Max(0, Array.IndexOf(ScalingNames, settings.Scaling));
            TreasureMap.IsChecked = settings.TreasureMap;
            RenderFps.Value = settings.RenderFps;
            SettingsPanel.IsVisible = true;
            ProgressPanel.IsVisible = false;
            NoticePanel.IsVisible = false;
            UpdateResolutionHint();
        }
        finally { _busy = false; RefreshState(); }
    }
    private async void Settings_Click(object? sender, RoutedEventArgs e)
    {
        try { await ShowSettings(); }
        catch (Exception error) { Notice(error.Message); }
    }
    private void CloseSettings_Click(object? sender, RoutedEventArgs e)
    { SettingsPanel.IsVisible = false; RefreshState(); }
    private void Resolution_Changed(object? sender, SelectionChangedEventArgs e)
    { if (_initialized) UpdateResolutionHint(); }
    private void UpdateResolutionHint()
    {
        int scale = Resolution.SelectedIndex + 1;
        var screen = Screens.ScreenFromWindow(this);
        string output = screen is null ? "your desktop resolution" : $"{screen.Bounds.Width} × {screen.Bounds.Height}";
        ResolutionHint.Text = $"{1280 * scale} × {720 * scale} internally → {output} display. Higher scales increase GPU load.";
    }
    private async Task ChangeSettings(string action)
    {
        var settings = new GraphicsSettings(Resolution.SelectedIndex + 1,
            ScalingNames[Math.Max(0, Scaling.SelectedIndex)], TreasureMap.IsChecked == true,
            (int)(RenderFps.Value ?? 0));
        var args = action == "apply" ? settings.Arguments(_backend.Options.State)
            : ["--action", action, "--state-root", _backend.Options.State, "--json"];
        await RunOperation("Updating settings", (log, token) =>
            _backend.ScriptAsync("set-graphics-experiment.sh", args, log, token));
    }
    private async void SaveSettings_Click(object? sender, RoutedEventArgs e) => await ChangeSettings("apply");
    private async void Reset_Click(object? sender, RoutedEventArgs e) => await ChangeSettings("reset");
    private async void Restore_Click(object? sender, RoutedEventArgs e) => await ChangeSettings("restore");

    private async Task OpenLocation(string path, bool directory = true)
    {
        try
        {
            if (directory) Directory.CreateDirectory(path);
            var result = await ProcessRunner.RunAsync("xdg-open", [path], _backend.Options.Repository);
            if (result.ExitCode != 0) Notice($"Could not open {path}: {result.Output}");
        }
        catch (Exception error) { Notice(error.Message); }
    }
    private async void Saves_Click(object? sender, RoutedEventArgs e) => await OpenLocation(_backend.Options.State);
    private async void Logs_Click(object? sender, RoutedEventArgs e) => await OpenLocation(Path.Combine(_backend.Options.Repository, ".local/logs"));
    private async void Reports_Click(object? sender, RoutedEventArgs e) => await OpenLocation(Path.Combine(_backend.Options.State, "reports"));
    private async void GitHub_Click(object? sender, RoutedEventArgs e) =>
        await OpenLocation(_backend.Distribution?.Url ?? "https://github.com/arcanite24/pinyon-shift", false);

    private async void Licenses_Click(object? sender, RoutedEventArgs e)
    {
        try
        {
            string ReadNotice(string name)
            {
                string installed = Path.Combine(AppContext.BaseDirectory, name);
                string source = Path.Combine(_backend.Options.Repository, name);
                return File.ReadAllText(File.Exists(installed) ? installed : source);
            }
            var notices = new Button { Content = "Open dependency license texts" };
            notices.Click += async (_, _) => await OpenLocation(Path.Combine(AppContext.BaseDirectory, "Notices"));
            var text = new TextBox
            {
                Text = ReadNotice("AUTHORS.md") + "\n\n" + ReadNotice("LICENSE")
                    + "\n\n" + ReadNotice("THIRD_PARTY_NOTICES.md"),
                IsReadOnly = true, TextWrapping = Avalonia.Media.TextWrapping.Wrap,
                HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Stretch
            };
            var content = new DockPanel { Margin = new Thickness(20), LastChildFill = true };
            var close = new Button { Content = "Close" };
            var buttons = new StackPanel
            {
                Orientation = Avalonia.Layout.Orientation.Horizontal, Spacing = 12,
                Margin = new Thickness(0, 12, 0, 0)
            };
            buttons.Children.Add(notices);
            buttons.Children.Add(close);
            DockPanel.SetDock(buttons, Dock.Bottom);
            content.Children.Add(buttons);
            content.Children.Add(text);
            var dialog = new Window
            {
                Title = "Credits and licenses", Width = 720, Height = 600,
                WindowStartupLocation = WindowStartupLocation.CenterOwner, Content = content
            };
            close.Click += (_, _) => dialog.Close();
            dialog.Opened += (_, _) => close.Focus();
            dialog.AddHandler(KeyDownEvent, (_, key) =>
            {
                if (key.Key == Key.Escape) { key.Handled = true; dialog.Close(); }
            }, RoutingStrategies.Tunnel);
            await dialog.ShowDialog(this);
        }
        catch (Exception error) { Notice($"Could not read licenses: {error.Message}"); }
    }

    private async void Versions_Click(object? sender, RoutedEventArgs e)
    {
        if (_backend.Distribution is not { } distribution) return;
        var status = new TextBlock { Text = "Loading Linux releases…", TextWrapping = Avalonia.Media.TextWrapping.Wrap };
        var choices = new ComboBox { HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Stretch };
        var download = new Button { Content = "Download selected version", IsEnabled = false };
        var dialog = new Window
        {
            Title = "Choose a Linux version", Width = 460, Height = 240, CanResize = false,
            WindowStartupLocation = WindowStartupLocation.CenterOwner,
            Content = new StackPanel { Margin = new Thickness(24), Spacing = 16,
                Children = { status, choices, download } }
        };
        using var cancellation = new CancellationTokenSource();
        dialog.Closed += (_, _) => cancellation.Cancel();
        dialog.Opened += async (_, _) =>
        {
            try
            {
                using var client = new System.Net.Http.HttpClient { Timeout = TimeSpan.FromSeconds(20) };
                client.DefaultRequestHeaders.UserAgent.ParseAdd("PinyonShift-Linux-Launcher/0.4.0");
                string json = await client.GetStringAsync(
                    $"https://api.github.com/repos/{distribution.Repository}/releases?per_page=100", cancellation.Token);
                using var document = JsonDocument.Parse(json);
                var tags = document.RootElement.EnumerateArray()
                    .Where(release => !release.GetProperty("draft").GetBoolean())
                    .Where(release => release.GetProperty("assets").EnumerateArray().Any(asset =>
                        asset.GetProperty("name").GetString()?.StartsWith("PinyonShift-Linux-", StringComparison.Ordinal) == true))
                    .Select(release => release.GetProperty("tag_name").GetString()!)
                    .Where(tag => !string.IsNullOrWhiteSpace(tag)).ToArray();
                choices.ItemsSource = tags;
                choices.SelectedIndex = tags.Length > 0 ? 0 : -1;
                download.IsEnabled = tags.Length > 0;
                status.Text = tags.Length > 0 ? $"Installed: {_backend.Version}. Choose a release to download."
                    : "No Linux releases have been published yet.";
            }
            catch (OperationCanceledException) { status.Text = "Release lookup was cancelled or timed out."; }
            catch (Exception error) { status.Text = $"Could not load versions: {error.Message}"; }
        };
        download.Click += async (_, _) =>
        {
            if (choices.SelectedItem is string tag)
                await OpenLocation($"{distribution.Url}/releases/tag/{Uri.EscapeDataString(tag)}", false);
        };
        await dialog.ShowDialog(this);
    }
}
