using System.Diagnostics;
using System.Formats.Tar;
using System.IO.Compression;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json.Serialization;

namespace HowCouldItBeYou.Launcher;

/// <summary>
/// "HCIBY-Launcher.json" in a GitHub release (written by Tools/PackLauncher.ps1 next to HCIBY-Launcher.zip and
/// HCIBY-Launcher-Linux.tar.gz): which launcher version those files hold and how to check them.
/// </summary>
public sealed class LauncherBuildInfo
{
    [JsonPropertyName("version")] public string Version { get; set; } = "";
    /// <summary>Per system ("win-x64", "linux-x64").</summary>
    [JsonPropertyName("files")] public Dictionary<string, LauncherBuildFile> Files { get; set; } = [];
}

public sealed class LauncherBuildFile
{
    /// <summary>Release asset with the launcher (.zip on Windows, .tar.gz on Linux).</summary>
    [JsonPropertyName("asset")] public string Asset { get; set; } = "";
    [JsonPropertyName("size")] public long Size { get; set; }
    [JsonPropertyName("sha256")] public string Sha256 { get; set; } = "";
    /// <summary>The launcher program inside the archive.</summary>
    [JsonPropertyName("program")] public string Program { get; set; } = "";
}

/// <summary>A newer launcher found in the releases.</summary>
public sealed record LauncherUpdate(Version Version, string Tag, string Url, LauncherBuildFile File);

/// <summary>
/// The launcher replaces its own program file with a newer build from the releases; launcher.json, language.txt
/// and the game folder stay as they are.
///
/// The running program file is never touched: a one-file .NET program reads parts of itself from its path while it
/// runs, so renaming or replacing it under a running launcher breaks that launcher. Instead the checked new program
/// waits next to the old one as "...new". To switch, the old launcher starts it as "...-update(.exe)" with
/// --finish-self-update and quits; that copy waits until the old process is gone, copies itself over the program
/// file, starts it and quits. A "...new" that could not be switched to right away (game running) is taken on the
/// next start.
/// </summary>
public static class SelfUpdater
{
    public const string InfoAsset = "HCIBY-Launcher.json";
    private const string FinishArg = "--finish-self-update";

    /// <summary>Key in <see cref="LauncherBuildInfo.Files"/> for this system.</summary>
    public static string Platform => OperatingSystem.IsWindows() ? "win-x64" : "linux-x64";

    /// <summary>Version of the running launcher (0.0.0 if unknown).</summary>
    public static Version CurrentVersion { get; } = ParseVersion(
        (Assembly.GetExecutingAssembly().GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "").Split('+')[0])
        ?? new Version(0, 0, 0);

    public static string? ProgramPath => Environment.ProcessPath;

    /// <summary>
    /// Only the shipped one-file launcher replaces itself, never a development build (bin\...\ with its DLLs)
    /// or a launcher started through "dotnet".
    /// </summary>
    public static bool CanReplaceItself
    {
        get
        {
#pragma warning disable IL3000 // Location is empty exactly in a one-file app, which is what this asks
            var oneFile = string.IsNullOrEmpty(typeof(SelfUpdater).Assembly.Location);
#pragma warning restore IL3000
            return oneFile && ProgramPath is { } path &&
                Path.GetFileNameWithoutExtension(path).Equals("HowCouldItBeYouLauncher", StringComparison.OrdinalIgnoreCase);
        }
    }

    public static Version? ParseVersion(string text) =>
        Version.TryParse(text.Trim().TrimStart('v', 'V'), out var version) ? Normalize(version) : null;

    /// <summary>0.5 and 0.5.0 are the same version.</summary>
    private static Version Normalize(Version v) => new(v.Major, v.Minor, Math.Max(v.Build, 0), Math.Max(v.Revision, 0));

    private static string Folder => Path.GetDirectoryName(ProgramPath!)!;
    private static string Pending => ProgramPath + ".new";
    private static string Partial => ProgramPath + ".new.part";
    public static string DownloadPath => ProgramPath + ".download";
    /// <summary>The copy that switches the program file (a name the system runs as a program).</summary>
    private static string Switcher => Path.Combine(Folder, OperatingSystem.IsWindows() ? "HowCouldItBeYouLauncher-update.exe" : "HowCouldItBeYouLauncher-update");

    /// <summary>A checked newer launcher waits next to this one.</summary>
    public static bool HasPending => CanReplaceItself && File.Exists(Pending);

    /// <summary>Checks the downloaded archive and unpacks the launcher program next to the running one as "...new".</summary>
    public static void Unpack(LauncherUpdate update, string archive)
    {
        var info = new FileInfo(archive);
        if (info.Length != update.File.Size)
        {
            throw new InvalidDataException($"Launcher download has {info.Length} bytes, expected {update.File.Size}");
        }
        using (var stream = File.OpenRead(archive))
        {
            var hash = Convert.ToHexStringLower(SHA256.HashData(stream));
            if (!hash.Equals(update.File.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException("Launcher download is damaged (SHA-256 does not match)");
            }
        }

        TryDelete(Partial);
        var program = update.File.Program.Replace('\\', '/');
        var found = false;
        if (update.File.Asset.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
        {
            using var zip = ZipFile.OpenRead(archive);
            var entry = zip.Entries.FirstOrDefault(e => e.FullName.Replace('\\', '/') == program);
            if (entry is not null)
            {
                entry.ExtractToFile(Partial, overwrite: true);
                found = true;
            }
        }
        else
        {
            using var file = File.OpenRead(archive);
            using var gzip = new GZipStream(file, CompressionMode.Decompress);
            using var tar = new TarReader(gzip);
            while (tar.GetNextEntry() is { } entry)
            {
                if (entry.EntryType is TarEntryType.RegularFile or TarEntryType.V7RegularFile && entry.Name.TrimStart('.', '/') == program)
                {
                    entry.ExtractToFile(Partial, overwrite: true);
                    found = true;
                    break;
                }
            }
        }
        if (!found)
        {
            throw new InvalidDataException($"{update.File.Asset} has no {program}");
        }
        if (new FileInfo(Partial).Length < 1_000_000)
        {
            throw new InvalidDataException("The new launcher program is suspiciously small");
        }
        if (!OperatingSystem.IsWindows())
        {
            File.SetUnixFileMode(Partial, File.GetUnixFileMode(ProgramPath!) | UnixFileMode.UserExecute | UnixFileMode.GroupExecute | UnixFileMode.OtherExecute);
        }
        File.Move(Partial, Pending, overwrite: true);
    }

    /// <summary>
    /// Starts the waiting new launcher as the switcher; the caller must quit right after. The switcher starts the
    /// program file with <paramref name="args"/> once it holds the new version.
    /// </summary>
    public static void LaunchPending(params string[] args)
    {
        File.Move(Pending, Switcher, overwrite: true);
        var start = new ProcessStartInfo(Switcher) { UseShellExecute = false, WorkingDirectory = LauncherConfig.BaseDirectory };
        start.ArgumentList.Add(FinishArg);
        start.ArgumentList.Add(Environment.ProcessId.ToString());
        start.ArgumentList.Add(ProgramPath!);
        foreach (var arg in args)
        {
            start.ArgumentList.Add(arg);
        }
        StartWithRetry(start);
    }

    public static bool IsFinishCall(string[] args) => args.Length >= 3 && args[0] == FinishArg;

    /// <summary>
    /// Runs in the switcher: waits for the old launcher to quit, copies itself over the program file and starts it.
    /// If the copy fails the old program is started, so the player always gets a launcher back.
    /// </summary>
    public static int Finish(string[] args)
    {
        var log = new List<string>();
        var target = args[2];
        var targetArgs = args[3..];
        try
        {
            try
            {
                using var old = Process.GetProcessById(int.Parse(args[1]));
                if (!old.WaitForExit(TimeSpan.FromSeconds(60)))
                {
                    log.Add("old launcher did not quit within 60 s, not switched");
                    return 2;
                }
            }
            catch (ArgumentException)
            {
                // already gone
            }

            for (var attempt = 1; ; attempt++)
            {
                try
                {
                    File.Copy(Environment.ProcessPath!, target, overwrite: true);
                    log.Add($"switched to {CurrentVersion}");
                    break;
                }
                catch (Exception ex) when (attempt < 40)
                {
                    // A virus scanner may still hold the old program for a moment
                    if (attempt == 1)
                    {
                        log.Add("waiting for the program file: " + ex.Message);
                    }
                    Thread.Sleep(250);
                }
                catch (Exception ex)
                {
                    log.Add("could not replace the program file, keeping the old launcher: " + ex.Message);
                    targetArgs = [];
                    break;
                }
            }
            var start = new ProcessStartInfo(target) { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(target)! };
            foreach (var arg in targetArgs)
            {
                start.ArgumentList.Add(arg);
            }
            StartWithRetry(start);
            return 0;
        }
        catch (Exception ex)
        {
            log.Add("switch failed: " + ex.Message);
            return 1;
        }
        finally
        {
            try
            {
                var file = Path.Combine(Path.GetDirectoryName(target)!, "update.log");
                File.AppendAllLines(file, log.Select(line => $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] launcher self-update: {line}"));
            }
            catch (Exception)
            {
                // no log
            }
        }
    }

    private static void StartWithRetry(ProcessStartInfo start)
    {
        // A virus scanner may hold a just written program for a moment
        for (var attempt = 1; ; attempt++)
        {
            try
            {
                Process.Start(start)?.Dispose();
                return;
            }
            catch (Exception) when (attempt < 5)
            {
                Thread.Sleep(500 * attempt);
            }
        }
    }

    /// <summary>
    /// Removes what a self-update left behind (not a waiting "...new"). The switcher is still running for a moment
    /// after it started this launcher, so this tries for a few seconds in the background.
    /// </summary>
    public static void CleanupLeftovers()
    {
        if (!CanReplaceItself)
        {
            return;
        }
        TryDelete(Partial);
        TryDelete(DownloadPath);
        if (!File.Exists(Switcher))
        {
            return;
        }
        _ = Task.Run(async () =>
        {
            for (var attempt = 0; attempt < 40 && File.Exists(Switcher); attempt++)
            {
                TryDelete(Switcher);
                await Task.Delay(250);
            }
        });
    }

    public static bool SwitcherLeftOver => CanReplaceItself && File.Exists(Switcher);

    private static void TryDelete(string path)
    {
        try { if (File.Exists(path)) File.Delete(path); } catch { /* still locked or not ours */ }
    }
}
