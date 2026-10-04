using Avalonia;

namespace PinyonShift.Launcher.Linux;

internal static class Program
{
    public static LauncherOptions Options { get; private set; } = null!;

    [STAThread]
    public static int Main(string[] args)
    {
        if (args.Contains("--help"))
        {
            Console.WriteLine("Pinyon Shift Linux launcher\n" +
                "  --repo-root DIR   Source checkout (otherwise discovered beside the launcher)\n" +
                "  --state-root DIR  Existing saves/settings (default: <repo>/.local/preview)\n" +
                "  --screenshot PNG Capture the window and exit (UI testing)\n" +
                "  --show-settings  Open the settings panel at startup");
            return 0;
        }
        try
        {
            Options = LauncherOptions.Parse(args);
            return AppBuilder.Configure<App>().UsePlatformDetect().LogToTrace()
                .StartWithClassicDesktopLifetime([]);
        }
        catch (Exception error)
        {
            Console.Error.WriteLine($"Pinyon Shift launcher: {error.Message}");
            return 1;
        }
    }
}
