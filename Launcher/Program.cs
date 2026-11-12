using Avalonia;

namespace HowCouldItBeYou.Launcher;

internal static class Program
{
    /// <summary>Only for the screenshot self-test: PNG path, state and notes file.</summary>
    public static (string Png, string? Preview, string? NotesFile)? Screenshot { get; private set; }

    [STAThread]
    private static int Main(string[] args)
    {
        // Headless test of the install path (used by Claude/CI, no window):
        //   HowCouldItBeYouLauncher(.exe) --selftest-install <zip-url> <tag>
        // Exit code 0 = installed, result in selftest.log next to the launcher.
        if (args.Length >= 3 && args[0] == "--selftest-install")
        {
            return SelfTestInstall(args[1], args[2]);
        }

        // Renders the window into a PNG (used to check the design without looking at the screen):
        //   HowCouldItBeYouLauncher(.exe) --selftest-screenshot <png> [update|install|busy|close|-] [notes.md]
        // notes.md replaces the latest release notes, to see how new ones will look before publishing them.
        if (args.Length >= 2 && args[0] == "--selftest-screenshot")
        {
            Screenshot = (Path.GetFullPath(args[1]), args.Length > 2 ? args[2] : null, args.Length > 3 ? args[3] : null);
        }
        else
        {
            GitHubUpdater.CleanupLeftovers();
        }

        return BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    // Also used by Avalonia's design tools
    public static AppBuilder BuildAvaloniaApp() =>
        AppBuilder.Configure<App>()
            .UsePlatformDetect();

    private static int SelfTestInstall(string url, string tag)
    {
        var log = Path.Combine(LauncherConfig.BaseDirectory, "selftest.log");
        try
        {
            var config = LauncherConfig.Load();
            using var updater = new GitHubUpdater(config);
            var release = new ReleaseInfo(tag, tag, "", Path.GetFileName(new Uri(url).LocalPath), url, 0, DateTimeOffset.Now);
            var lines = new List<string>();
            var progress = new Progress<(Phrase Status, double? Fraction)>(_ => { });
            updater.InstallAsync(release, progress, CancellationToken.None).GetAwaiter().GetResult();
            lines.Add($"installed={updater.GetInstalledVersion()}");
            lines.Add($"exe={updater.FindGameExecutable()}");
            File.WriteAllLines(log, lines);
            return updater.GetInstalledVersion() == tag && updater.FindGameExecutable() is not null ? 0 : 2;
        }
        catch (Exception ex)
        {
            File.WriteAllText(log, "error=" + ex);
            return 1;
        }
    }
}
