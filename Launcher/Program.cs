namespace HowCouldItBeYou.Launcher;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        // Headless test of the install path (used by Claude/CI, no window):
        //   HowCouldItBeYouLauncher.exe --selftest-install <zip-url> <tag>
        // Exit code 0 = installed, result in selftest.log next to the exe.
        if (args.Length >= 3 && args[0] == "--selftest-install")
        {
            return SelfTestInstall(args[1], args[2]);
        }

        ApplicationConfiguration.Initialize();
        GitHubUpdater.CleanupLeftovers();
        Application.Run(new MainForm());
        return 0;
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
            var progress = new Progress<(string Status, double? Fraction)>(_ => { });
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
