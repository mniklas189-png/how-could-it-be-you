using System.IO.Compression;
using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.Json.Serialization;
using HowCouldItBeYou.Launcher.Patching;

namespace HowCouldItBeYou.Launcher;

/// <param name="ManifestUrl">The release's "...manifest.json" for this system, if it has one (small updates, see PatchManifest).</param>
public sealed record ReleaseInfo(string Tag, string Name, string Notes, string AssetName, string DownloadUrl, long AssetSize,
    DateTimeOffset PublishedAt, string? ManifestUrl = null);

/// <summary>How an install went: only the changes (Patched) or the whole zip, and how much was downloaded.</summary>
public sealed record InstallResult(bool Patched, long Downloaded);

public enum UpdateState
{
    /// <summary>Game not installed yet, a release exists.</summary>
    NotInstalled,
    /// <summary>A newer release than the installed one exists.</summary>
    UpdateAvailable,
    /// <summary>Installed version = latest release.</summary>
    UpToDate,
    /// <summary>The repository has no release with a matching .zip.</summary>
    NoRelease,
}

public sealed record UpdateCheckResult(UpdateState State, ReleaseInfo? Latest, string? InstalledVersion);

/// <summary>
/// Checks GitHub Releases of the configured repository and installs a release: when the game is installed and
/// the release has a manifest, only the changes (<see cref="PatchUpdater"/>); otherwise, or if that fails before
/// anything was changed, the release .zip. Installed version is stored in &lt;InstallFolder&gt;/version.txt (= the release tag).
/// </summary>
public sealed class GitHubUpdater : IDisposable
{
    private const string VersionFileName = "version.txt";
    private const string LogFileName = "update.log";
    private readonly LauncherConfig _config;
    private readonly HttpClient _http;
    private readonly PatchUpdater _patcher;
    private readonly Dictionary<string, PatchManifest> _manifests = new(StringComparer.Ordinal);
    /// <summary>Releases of the last check (the launcher's own update looks into them too).</summary>
    private List<GhRelease> _releases = [];

    /// <summary>GitHub API; HC_LAUNCHER_API points it at a local test server (--selftest-selfupdate).</summary>
    private static string ApiBase => (Environment.GetEnvironmentVariable("HC_LAUNCHER_API") is { Length: > 0 } api ? api : "https://api.github.com").TrimEnd('/');

    /// <param name="packUrl">Where chunk packs are downloaded from (tests: a local server); default GitHub release assets.</param>
    public GitHubUpdater(LauncherConfig config, Func<PatchPack, string>? packUrl = null)
    {
        _config = config;
        _http = new HttpClient(new HttpClientHandler { AutomaticDecompression = DecompressionMethods.All })
        {
            Timeout = Timeout.InfiniteTimeSpan,
        };
        // GitHub requires a User-Agent; a generic client name is enough.
        _http.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("HowCouldItBeYou-Launcher", "0.5"));
        _http.DefaultRequestHeaders.Add("X-GitHub-Api-Version", "2022-11-28");
        if (!string.IsNullOrWhiteSpace(config.GitHubToken))
        {
            _http.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", config.GitHubToken);
        }
        _patcher = new PatchUpdater(_http, config.InstallPath, LauncherConfig.BaseDirectory,
            packUrl ?? (pack => PatchUpdater.GitHubPackUrl(config.Repository, pack)));
    }

    public string? GetInstalledVersion()
    {
        var file = Path.Combine(_config.InstallPath, VersionFileName);
        return File.Exists(file) ? File.ReadAllText(file).Trim() : null;
    }

    public string? FindGameExecutable()
    {
        if (!Directory.Exists(_config.InstallPath))
        {
            return null;
        }
        var direct = Path.Combine(_config.InstallPath, _config.GameExecutable);
        if (File.Exists(direct))
        {
            return direct;
        }
        // Packaged UE builds put the launcher .exe at the top and the real one in Binaries; prefer the top-most.
        return Directory.EnumerateFiles(_config.InstallPath, _config.GameExecutable, SearchOption.AllDirectories)
            .OrderBy(p => p.Count(c => c == Path.DirectorySeparatorChar))
            .FirstOrDefault();
    }

    public async Task<UpdateCheckResult> CheckAsync(CancellationToken ct)
    {
        var installed = GetInstalledVersion();
        var latest = await GetLatestReleaseAsync(ct);
        if (latest is null)
        {
            return new UpdateCheckResult(UpdateState.NoRelease, null, installed);
        }
        if (installed is null || FindGameExecutable() is null)
        {
            return new UpdateCheckResult(UpdateState.NotInstalled, latest, installed);
        }
        var state = string.Equals(installed, latest.Tag, StringComparison.OrdinalIgnoreCase)
            ? UpdateState.UpToDate
            : UpdateState.UpdateAvailable;
        return new UpdateCheckResult(state, latest, installed);
    }

    private async Task<ReleaseInfo?> GetLatestReleaseAsync(CancellationToken ct)
    {
        var url = $"{ApiBase}/repos/{_config.Repository}/releases?per_page=20";
        using var response = await _http.GetAsync(url, ct);
        if (response.StatusCode == HttpStatusCode.NotFound)
        {
            throw new PhraseException(new(
                $"Repository \"{_config.Repository}\" nicht gefunden. Ist es privat? Dann muss es öffentlich sein (oder ein Token in launcher.json stehen).",
                $"Repository \"{_config.Repository}\" not found. Is it private? Then it has to be public (or launcher.json needs a token)."));
        }
        if (response.StatusCode == HttpStatusCode.Forbidden)
        {
            throw new PhraseException(new(
                "GitHub hat die Anfrage abgelehnt (vermutlich Limit erreicht). Bitte in ein paar Minuten erneut versuchen.",
                "GitHub refused the request (probably the rate limit). Please try again in a few minutes."));
        }
        response.EnsureSuccessStatusCode();

        await using var stream = await response.Content.ReadAsStreamAsync(ct);
        var releases = await JsonSerializer.DeserializeAsync<List<GhRelease>>(stream, cancellationToken: ct) ?? [];
        _releases = releases;

        foreach (var release in releases.Where(r => !r.Draft && (_config.IncludePrereleases || !r.Prerelease))
                                        .OrderByDescending(r => r.PublishedAt))
        {
            var asset = release.Assets.FirstOrDefault(a =>
                a.Name.EndsWith(".zip", StringComparison.OrdinalIgnoreCase) &&
                a.Name.Contains(_config.AssetNameContains, StringComparison.OrdinalIgnoreCase));
            if (asset is null)
            {
                continue;
            }
            var manifest = release.Assets.FirstOrDefault(a =>
                a.Name.EndsWith(PatchManifest.FileSuffix, StringComparison.OrdinalIgnoreCase) &&
                a.Name.Contains(_config.AssetNameContains, StringComparison.OrdinalIgnoreCase));
            // Private repos (token set): only the API asset URL accepts the token, and the chunk packs are not
            // reachable that way, so those launchers always take the zip.
            var tokenMode = !string.IsNullOrWhiteSpace(_config.GitHubToken);
            var downloadUrl = tokenMode ? asset.ApiUrl : asset.BrowserDownloadUrl;
            var title = string.IsNullOrWhiteSpace(release.Name) ? release.TagName : release.Name;
            return new ReleaseInfo(release.TagName, title, release.Body ?? "",
                asset.Name, downloadUrl, asset.Size, release.PublishedAt, tokenMode ? null : manifest?.BrowserDownloadUrl);
        }
        return null;
    }

    /// <summary>
    /// A launcher newer than this one in the releases of the last check: the newest release with an
    /// HCIBY-Launcher.json decides. Null when there is none, or this launcher can't replace itself.
    /// </summary>
    public async Task<LauncherUpdate?> FindLauncherUpdateAsync(CancellationToken ct)
    {
        if (!SelfUpdater.CanReplaceItself || !string.IsNullOrWhiteSpace(_config.GitHubToken))
        {
            return null;
        }
        var release = _releases.Where(r => !r.Draft && (_config.IncludePrereleases || !r.Prerelease))
            .OrderByDescending(r => r.PublishedAt)
            .FirstOrDefault(r => r.Assets.Any(a => a.Name.Equals(SelfUpdater.InfoAsset, StringComparison.OrdinalIgnoreCase)));
        if (release is null)
        {
            return null;
        }
        var infoAsset = release.Assets.First(a => a.Name.Equals(SelfUpdater.InfoAsset, StringComparison.OrdinalIgnoreCase));
        var info = JsonSerializer.Deserialize<LauncherBuildInfo>(await _http.GetStringAsync(infoAsset.BrowserDownloadUrl, ct));
        var version = info is null ? null : SelfUpdater.ParseVersion(info.Version);
        if (version is null || version <= SelfUpdater.CurrentVersion || !info!.Files.TryGetValue(SelfUpdater.Platform, out var file))
        {
            return null;
        }
        var asset = release.Assets.FirstOrDefault(a => a.Name.Equals(file.Asset, StringComparison.OrdinalIgnoreCase));
        if (asset is null || string.IsNullOrEmpty(file.Sha256) || string.IsNullOrEmpty(file.Program))
        {
            Log($"launcher {version} in {release.TagName}: {file.Asset} missing or incomplete, no self-update");
            return null;
        }
        return new LauncherUpdate(version, release.TagName, asset.BrowserDownloadUrl, file);
    }

    /// <summary>Downloads and checks a newer launcher; it then waits next to this one (<see cref="SelfUpdater.LaunchPending"/>).</summary>
    public async Task DownloadLauncherAsync(LauncherUpdate update, CancellationToken ct)
    {
        var download = SelfUpdater.DownloadPath;
        try
        {
            using (var response = await _http.GetAsync(update.Url, HttpCompletionOption.ResponseHeadersRead, ct))
            {
                response.EnsureSuccessStatusCode();
                await using var source = await response.Content.ReadAsStreamAsync(ct);
                await using var target = File.Create(download);
                await source.CopyToAsync(target, ct);
            }
            await Task.Run(() => SelfUpdater.Unpack(update, download), CancellationToken.None);
            Log($"launcher {SelfUpdater.CurrentVersion} -> {update.Version} (from {update.Tag}) downloaded and checked");
        }
        catch (Exception ex)
        {
            Log($"launcher self-update to {update.Version} failed: {ex.Message}");
            throw;
        }
        finally
        {
            TryDelete(download);
        }
    }

    /// <summary>Whether this release can be installed as a small update over the installed game.</summary>
    public bool CanPatch(ReleaseInfo release) =>
        release.ManifestUrl is not null && GetInstalledVersion() is not null && FindGameExecutable() is not null;

    /// <summary>
    /// How much a small update to this release would download (reads the installed files, a few seconds),
    /// or null when it has to be the whole zip.
    /// </summary>
    public async Task<long?> EstimateAsync(ReleaseInfo release, CancellationToken ct)
    {
        if (!CanPatch(release))
        {
            return null;
        }
        try
        {
            var manifest = await LoadManifestAsync(release, ct);
            var plan = await Task.Run(() => _patcher.Plan(manifest, null, ct), ct);
            return WorthIt(plan, release) ? plan.DownloadBytes : null;
        }
        catch (PatchFallbackException)
        {
            return null;
        }
    }

    private static bool WorthIt(PatchPlan plan, ReleaseInfo release) =>
        release.AssetSize <= 0 || plan.DownloadBytes < release.AssetSize * 0.75;

    private async Task<PatchManifest> LoadManifestAsync(ReleaseInfo release, CancellationToken ct)
    {
        lock (_manifests)
        {
            if (_manifests.TryGetValue(release.ManifestUrl!, out var cached))
            {
                return cached;
            }
        }
        var manifest = await _patcher.LoadManifestAsync(release.ManifestUrl!, ct);
        if (!string.Equals(manifest.Tag, release.Tag, StringComparison.OrdinalIgnoreCase))
        {
            throw new PatchFallbackException($"manifest is for {manifest.Tag}, release is {release.Tag}");
        }
        lock (_manifests)
        {
            _manifests[release.ManifestUrl!] = manifest;
        }
        return manifest;
    }

    /// <summary>
    /// Installs the release: only the changes if possible, otherwise the whole zip. Either way a failed or
    /// cancelled update leaves the player with the version they had.
    /// </summary>
    public async Task<InstallResult> InstallAsync(ReleaseInfo release, IProgress<(Phrase Status, double? Fraction)> progress, CancellationToken ct)
    {
        if (CanPatch(release))
        {
            try
            {
                return await InstallPatchAsync(release, progress, ct);
            }
            catch (PatchFallbackException ex)
            {
                Log($"{release.Tag}: small update not possible, taking the zip: {ex.Message}\n{ex.InnerException}");
                progress.Report((new("Kleines Update nicht möglich – lade das ganze Spiel ...", "A small update isn't possible – downloading the whole game ..."), null));
            }
        }
        await InstallZipAsync(release, progress, ct);
        return new InstallResult(false, release.AssetSize);
    }

    private async Task<InstallResult> InstallPatchAsync(ReleaseInfo release, IProgress<(Phrase Status, double? Fraction)> progress, CancellationToken ct)
    {
        progress.Report((new("Lade die Liste der Änderungen ...", "Loading the list of changes ..."), null));
        var manifest = await LoadManifestAsync(release, ct);
        var checking = new Phrase("Vergleiche die installierten Dateien ...", "Checking the installed files ...");
        progress.Report((checking, 0));
        var planProgress = new Progress<double>(f => progress.Report((checking, f)));
        var plan = await Task.Run(() => _patcher.Plan(manifest, planProgress, ct), ct);
        Log(PatchUpdater.Describe(plan));
        if (!WorthIt(plan, release))
        {
            throw new PatchFallbackException($"the changes ({plan.DownloadBytes} bytes) are not much smaller than the zip");
        }
        await _patcher.ApplyAsync(plan, release.Tag, progress, ct);
        Log($"{release.Tag}: installed as a small update, {plan.DownloadBytes} bytes downloaded");
        progress.Report((new(
            $"Version {release.Tag} installiert – nur {FormatSize(plan.DownloadBytes)} geladen.",
            $"Version {release.Tag} installed – only {FormatSize(plan.DownloadBytes)} downloaded."), 1));
        return new InstallResult(true, plan.DownloadBytes);
    }

    /// <summary>
    /// Downloads the release .zip and replaces the installed game.
    /// The old install is only removed after the new one was extracted completely, so a failed
    /// or cancelled update never leaves the player without a working game.
    /// </summary>
    private async Task InstallZipAsync(ReleaseInfo release, IProgress<(Phrase Status, double? Fraction)> progress, CancellationToken ct)
    {
        var baseDir = LauncherConfig.BaseDirectory;
        var tempZip = Path.Combine(baseDir, "update.download");
        var staging = Path.Combine(baseDir, "update.staging");
        var backup = Path.Combine(baseDir, "update.previous");
        var install = _config.InstallPath;

        TryDelete(tempZip);
        TryDeleteDir(staging);

        // 1) Download
        progress.Report((new($"Lade {release.AssetName} ...", $"Downloading {release.AssetName} ..."), 0));
        using (var request = new HttpRequestMessage(HttpMethod.Get, release.DownloadUrl))
        {
            if (!string.IsNullOrWhiteSpace(_config.GitHubToken))
            {
                // Private repos: assets must be fetched through the API URL with octet-stream
                request.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/octet-stream"));
            }
            using var response = await _http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, ct);
            response.EnsureSuccessStatusCode();
            var total = response.Content.Headers.ContentLength ?? release.AssetSize;
            await using var source = await response.Content.ReadAsStreamAsync(ct);
            await using var target = File.Create(tempZip);
            var buffer = new byte[1 << 20];
            long done = 0;
            var lastReport = DateTime.MinValue;
            int read;
            while ((read = await source.ReadAsync(buffer, ct)) > 0)
            {
                await target.WriteAsync(buffer.AsMemory(0, read), ct);
                done += read;
                if ((DateTime.UtcNow - lastReport).TotalMilliseconds > 100)
                {
                    lastReport = DateTime.UtcNow;
                    var fraction = total > 0 ? (double)done / total : (double?)null;
                    progress.Report((new($"Lade herunter: {FormatSize(done)} / {FormatSize(total)}", $"Downloading: {FormatSize(done)} / {FormatSize(total)}"), fraction));
                }
            }
        }

        // 2) Extract into staging
        progress.Report((new("Entpacke ...", "Unpacking ..."), null));
        await Task.Run(() =>
        {
            ZipFile.ExtractToDirectory(tempZip, staging, overwriteFiles: true);
            // If the zip contains a single top folder (e.g. "Windows/"), use its content.
            var entries = Directory.GetFileSystemEntries(staging);
            var root = entries.Length == 1 && Directory.Exists(entries[0]) ? entries[0] : staging;
            File.WriteAllText(Path.Combine(root, VersionFileName), release.Tag);
            MarkExecutables(root);

            // 3) Swap: current -> backup, staging -> current, then delete backup
            TryDeleteDir(backup);
            if (Directory.Exists(install))
            {
                Directory.Move(install, backup);
            }
            try
            {
                Directory.Move(root, install);
            }
            catch
            {
                if (Directory.Exists(backup) && !Directory.Exists(install))
                {
                    Directory.Move(backup, install);
                }
                throw;
            }
            TryDeleteDir(backup);
            TryDeleteDir(staging);
        }, ct);

        TryDelete(tempZip);
        progress.Report((new($"Version {release.Tag} installiert.", $"Version {release.Tag} installed."), 1));
    }

    /// <summary>update.log next to the launcher: what the last updates did (kept short).</summary>
    private static void Log(string text)
    {
        try
        {
            var path = Path.Combine(LauncherConfig.BaseDirectory, LogFileName);
            if (File.Exists(path) && new FileInfo(path).Length > 256 * 1024)
            {
                File.Delete(path);
            }
            File.AppendAllText(path, $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {text.TrimEnd()}\n");
        }
        catch (Exception)
        {
            // read-only folder: no log
        }
    }

    /// <summary>
    /// Linux: a zip made on Windows carries no "executable" flag, so the start script and the programs in
    /// Binaries/Linux would not start. Sets it on *.sh and on every file in a Binaries/Linux folder.
    /// </summary>
    public static void MarkExecutables(string root)
    {
        if (OperatingSystem.IsWindows() || !Directory.Exists(root))
        {
            return;
        }
        var separator = Path.DirectorySeparatorChar;
        var binaries = $"{separator}Binaries{separator}Linux{separator}";
        foreach (var file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
        {
            if (file.EndsWith(".sh", StringComparison.Ordinal) || file.Contains(binaries, StringComparison.Ordinal))
            {
                MarkExecutable(file);
            }
        }
    }

    public static void MarkExecutable(string file)
    {
        if (OperatingSystem.IsWindows())
        {
            return;
        }
        try
        {
            var mode = File.GetUnixFileMode(file);
            const UnixFileMode execute = UnixFileMode.UserExecute | UnixFileMode.GroupExecute | UnixFileMode.OtherExecute;
            if ((mode & execute) != execute)
            {
                File.SetUnixFileMode(file, mode | execute);
            }
        }
        catch (Exception)
        {
            // not our file (e.g. another user's install): starting it will report the problem
        }
    }

    public static void CleanupLeftovers()
    {
        var baseDir = LauncherConfig.BaseDirectory;
        TryDelete(Path.Combine(baseDir, "update.download"));
        TryDeleteDir(Path.Combine(baseDir, "update.staging"));
    }

    private static void TryDelete(string path)
    {
        try { if (File.Exists(path)) File.Delete(path); } catch { /* ignored */ }
    }

    private static void TryDeleteDir(string path)
    {
        try { if (Directory.Exists(path)) Directory.Delete(path, recursive: true); } catch { /* ignored */ }
    }

    public static string FormatSize(long bytes) => bytes switch
    {
        >= 1L << 30 => $"{bytes / (double)(1L << 30):0.00} GB",
        >= 1L << 20 => $"{bytes / (double)(1L << 20):0.0} MB",
        >= 1L << 10 => $"{bytes / (double)(1L << 10):0} KB",
        _ => $"{bytes} B",
    };

    public void Dispose() => _http.Dispose();

    private sealed class GhRelease
    {
        [JsonPropertyName("tag_name")] public string TagName { get; set; } = "";
        [JsonPropertyName("name")] public string? Name { get; set; }
        [JsonPropertyName("body")] public string? Body { get; set; }
        [JsonPropertyName("draft")] public bool Draft { get; set; }
        [JsonPropertyName("prerelease")] public bool Prerelease { get; set; }
        [JsonPropertyName("published_at")] public DateTimeOffset PublishedAt { get; set; }
        [JsonPropertyName("assets")] public List<GhAsset> Assets { get; set; } = [];
    }

    private sealed class GhAsset
    {
        [JsonPropertyName("name")] public string Name { get; set; } = "";
        [JsonPropertyName("size")] public long Size { get; set; }
        [JsonPropertyName("browser_download_url")] public string BrowserDownloadUrl { get; set; } = "";
        [JsonPropertyName("url")] public string ApiUrl { get; set; } = "";
    }
}
