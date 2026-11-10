using System.Text.Json;

namespace HowCouldItBeYou.Launcher;

/// <summary>Settings from launcher.json next to the .exe.</summary>
public sealed class LauncherConfig
{
    /// <summary>GitHub "owner/name" whose releases contain the packaged game.</summary>
    public string Repository { get; set; } = "mniklas189-png/how-could-it-be-you";

    /// <summary>The release asset (.zip) whose name contains this text is downloaded.</summary>
    public string AssetNameContains { get; set; } = "HowCouldItBeYou-Win64";

    /// <summary>Folder (relative to the launcher) the game is installed into.</summary>
    public string InstallFolder { get; set; } = "Game";

    /// <summary>Game .exe inside the install folder (searched recursively if not at the top level).</summary>
    public string GameExecutable { get; set; } = "HowCouldItBeYou.exe";

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
        return JsonSerializer.Deserialize<LauncherConfig>(File.ReadAllText(path), options) ?? new LauncherConfig();
    }
}
