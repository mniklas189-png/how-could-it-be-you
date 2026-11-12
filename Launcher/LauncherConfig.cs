using System.Text.Json;

namespace HowCouldItBeYou.Launcher;

/// <summary>Settings from launcher.json next to the launcher (.exe on Windows, the binary on Linux).</summary>
public sealed class LauncherConfig
{
    /// <summary>GitHub "owner/name" whose releases contain the packaged game.</summary>
    public string Repository { get; set; } = "mniklas189-png/how-could-it-be-you";

    /// <summary>
    /// The release asset (.zip) whose name contains this text is downloaded.
    /// Every release has one zip per system: HowCouldItBeYou-Win64-v….zip and HowCouldItBeYou-Linux-v….zip.
    /// </summary>
    public string AssetNameContains { get; set; } = OperatingSystem.IsWindows() ? "HowCouldItBeYou-Win64" : "HowCouldItBeYou-Linux";

    /// <summary>Folder (relative to the launcher) the game is installed into.</summary>
    public string InstallFolder { get; set; } = "Game";

    /// <summary>
    /// Game start file inside the install folder (searched recursively if not at the top level):
    /// HowCouldItBeYou.exe on Windows, the start script HowCouldItBeYou.sh of the Linux package.
    /// </summary>
    public string GameExecutable { get; set; } = OperatingSystem.IsWindows() ? "HowCouldItBeYou.exe" : "HowCouldItBeYou.sh";

    /// <summary>Also offer pre-releases (e.g. test builds).</summary>
    public bool IncludePrereleases { get; set; }

    /// <summary>
    /// Optional, only for private repositories while testing. Never ship a launcher with a token to other people:
    /// anyone could read it out of the file.
    /// </summary>
    public string? GitHubToken { get; set; }

    public static string BaseDirectory => AppContext.BaseDirectory;

    public string InstallPath => Path.GetFullPath(Path.Combine(BaseDirectory, InstallFolder));

    public static LauncherConfig Load()
    {
        var path = Path.Combine(BaseDirectory, "launcher.json");
        if (!File.Exists(path))
        {
            var fresh = new LauncherConfig();
            File.WriteAllText(path, JsonSerializer.Serialize(fresh, new JsonSerializerOptions { WriteIndented = true }));
            return fresh;
        }
        var options = new JsonSerializerOptions { PropertyNameCaseInsensitive = true, ReadCommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true };
        var config = JsonSerializer.Deserialize<LauncherConfig>(File.ReadAllText(path), options) ?? new LauncherConfig();
        if (!OperatingSystem.IsWindows())
        {
            // A launcher.json copied over from Windows would download and look for the Windows game
            if (config.AssetNameContains.EndsWith("-Win64", StringComparison.OrdinalIgnoreCase))
            {
                config.AssetNameContains = config.AssetNameContains[..^"-Win64".Length] + "-Linux";
            }
            if (config.GameExecutable.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
            {
                config.GameExecutable = Path.ChangeExtension(config.GameExecutable, ".sh");
            }
        }
        return config;
    }
}
