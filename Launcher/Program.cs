using Avalonia;

namespace HowCouldItBeYou.Launcher;

internal static class Program
{
    /// <summary>Only for the screenshot self-test: PNG path, state and notes file.</summary>
    public static (string Png, string? Preview, string? NotesFile)? Screenshot { get; private set; }

    [STAThread]
    private static int Main(string[] args)
    {
        // The new launcher switching itself in (see SelfUpdater): before anything else, no window
        if (SelfUpdater.IsFinishCall(args))
        {
            return SelfUpdater.Finish(args);
        }

        // Headless test of the install path (used by Claude/CI, no window):
        //   HowCouldItBeYouLauncher(.exe) --selftest-install <zip-url> <tag>
        // Exit code 0 = installed, result in selftest.log next to the launcher.
        if (args.Length >= 3 && args[0] == "--selftest-install")
        {
            return SelfTestInstall(args[1], args[2]);
        }

        // Headless test of a small update (only the changes) against a server with GitHub's folder layout:
        //   HowCouldItBeYouLauncher(.exe) --selftest-patch <base-url> <tag> [zip-size]
        // reads <base-url>/<tag>/HowCouldItBeYou-<Win64|Linux>-<tag>.manifest.json, packs from <base-url>/<pack tag>/<asset>,
        // and falls back to <base-url>/<tag>/HowCouldItBeYou-<...>-<tag>.zip. Result in selftest.log, exit 0 = installed.
        if (args.Length >= 3 && args[0] == "--selftest-patch")
        {
            return SelfTestPatch(args[1].TrimEnd('/'), args[2], args.Length > 3 ? long.Parse(args[3]) : 0);
        }

        // Headless test of the launcher replacing itself (only the published one-file launcher can):
        //   HowCouldItBeYouLauncher(.exe) --selftest-selfupdate <api-base>
        // reads <api-base>/repos/<repo>/releases like the GitHub API, installs a newer launcher from there and starts
        // it with --selftest-after-update, which adds its own version to selftest.log. Exit 0 = new launcher started.
        if (args.Length >= 2 && args[0] == "--selftest-selfupdate")
        {
            return SelfTestSelfUpdate(args[1]);
        }
        if (args.Length >= 2 && args[0] == "--selftest-after-update")
        {
            return SelfTestAfterUpdate(args[1]);
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
            SelfUpdater.CleanupLeftovers();
            // Started by the switcher right after it put this version in place
            if (args.Length >= 2 && args[0] == "--after-self-update")
            {
                UpdatedFrom = args[1];
            }
            // A newer launcher downloaded last time (it could not switch then, e.g. a game was running): switch now
            else if (SelfUpdater.HasPending)
            {
                try
                {
                    SelfUpdater.LaunchPending("--after-self-update", SelfUpdater.CurrentVersion.ToString(3));
                    return 0;
                }
                catch (Exception)
                {
                    // start this launcher as it is
                }
            }
        }

        return BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    /// <summary>Version of the launcher that just replaced itself with this one (shown once in the status line).</summary>
    public static string? UpdatedFrom { get; private set; }

    private static int SelfTestSelfUpdate(string apiBase)
    {
        var log = Path.Combine(LauncherConfig.BaseDirectory, "selftest.log");
        var lines = new List<string> { $"before={SelfUpdater.CurrentVersion}", $"canReplace={SelfUpdater.CanReplaceItself}" };
        try
        {
            Environment.SetEnvironmentVariable("HC_LAUNCHER_API", apiBase);
            using var updater = new GitHubUpdater(LauncherConfig.Load());
            updater.CheckAsync(CancellationToken.None).GetAwaiter().GetResult();
            var update = updater.FindLauncherUpdateAsync(CancellationToken.None).GetAwaiter().GetResult();
            lines.Add($"found={update?.Version} tag={update?.Tag}");
            if (update is null)
            {
                File.WriteAllLines(log, lines);
                return 2;
            }
            updater.DownloadLauncherAsync(update, CancellationToken.None).GetAwaiter().GetResult();
            lines.Add($"pending={SelfUpdater.HasPending}");
            File.WriteAllLines(log, lines);
            SelfUpdater.LaunchPending("--selftest-after-update", log);
            return 0;
        }
        catch (Exception ex)
        {
            lines.Add("error=" + ex);
            File.WriteAllLines(log, lines);
            return 1;
        }
    }

    private static int SelfTestAfterUpdate(string log)
    {
        SelfUpdater.CleanupLeftovers();
        // The switcher is still running for a moment; cleanup retries in the background
        for (var i = 0; i < 40 && SelfUpdater.SwitcherLeftOver; i++)
        {
            Thread.Sleep(250);
        }
        File.AppendAllLines(log, [$"after={SelfUpdater.CurrentVersion}", $"switcherRemoved={!SelfUpdater.SwitcherLeftOver}",
            $"pendingLeft={SelfUpdater.HasPending}", $"program={SelfUpdater.ProgramPath}"]);
        return 0;
    }

    // Also used by Avalonia's design tools
    public static AppBuilder BuildAvaloniaApp() =>
        AppBuilder.Configure<App>()
            .UsePlatformDetect();

    private static int SelfTestPatch(string baseUrl, string tag, long zipSize)
    {
        var log = Path.Combine(LauncherConfig.BaseDirectory, "selftest.log");
        try
        {
            var config = LauncherConfig.Load();
            using var updater = new GitHubUpdater(config, pack => $"{baseUrl}/{Uri.EscapeDataString(pack.Tag)}/{Uri.EscapeDataString(pack.Asset)}");
            var assetBase = $"{config.AssetNameContains}-{tag}";
            var release = new ReleaseInfo(tag, tag, "", assetBase + ".zip", $"{baseUrl}/{tag}/{assetBase}.zip", zipSize, DateTimeOffset.Now,
                $"{baseUrl}/{tag}/{assetBase}{Patching.PatchManifest.FileSuffix}");
            var lines = new List<string>();
            var estimate = updater.EstimateAsync(release, CancellationToken.None).GetAwaiter().GetResult();
            lines.Add($"estimate={estimate}");
            var watch = System.Diagnostics.Stopwatch.StartNew();
            var progress = new Progress<(Phrase Status, double? Fraction)>(p => Console.WriteLine($"{p.Fraction:0.00} {p.Status.En}"));
            var result = updater.InstallAsync(release, progress, CancellationToken.None).GetAwaiter().GetResult();
            lines.Add($"patched={result.Patched}");
            lines.Add($"downloaded={result.Downloaded}");
            lines.Add($"seconds={watch.Elapsed.TotalSeconds:0.0}");
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
