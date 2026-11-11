using System.IO.Compression;
using System.Net;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace HowCouldItBeYou.Launcher;

public sealed record ReleaseInfo(string Tag, string Name, string Notes, string AssetName, string DownloadUrl, long AssetSize, DateTimeOffset PublishedAt);

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
/// Checks GitHub Releases of the configured repository and installs the release .zip.
/// Installed version is stored in &lt;InstallFolder&gt;/version.txt (= the release tag).
/// </summary>
public sealed class GitHubUpdater : IDisposable
{
    private const string VersionFileName = "version.txt";
    private readonly LauncherConfig _config;
    private readonly HttpClient _http;

    public GitHubUpdater(LauncherConfig config)
    {
        _config = config;
        _http = new HttpClient(new HttpClientHandler { AutomaticDecompression = DecompressionMethods.All })
        {
            Timeout = Timeout.InfiniteTimeSpan,
        };
        // GitHub requires a User-Agent; a generic client name is enough.
        _http.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("HowCouldItBeYou-Launcher", "0.1"));
        _http.DefaultRequestHeaders.Add("X-GitHub-Api-Version", "2022-11-28");
        if (!string.IsNullOrWhiteSpace(config.GitHubToken))
        {
            _http.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", config.GitHubToken);
        }
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
        var url = $"https://api.github.com/repos/{_config.Repository}/releases?per_page=20";
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
            // Private repos (token set): only the API asset URL accepts the token.
            var downloadUrl = string.IsNullOrWhiteSpace(_config.GitHubToken) ? asset.BrowserDownloadUrl : asset.ApiUrl;
            var title = string.IsNullOrWhiteSpace(release.Name) ? release.TagName : release.Name;
            return new ReleaseInfo(release.TagName, title, release.Body ?? "",
                asset.Name, downloadUrl, asset.Size, release.PublishedAt);
        }
        return null;
    }

    /// <summary>
    /// Downloads the release .zip and replaces the installed game.
    /// The old install is only removed after the new one was extracted completely, so a failed
    /// or cancelled update never leaves the player without a working game.
    /// </summary>
    public async Task InstallAsync(ReleaseInfo release, IProgress<(Phrase Status, double? Fraction)> progress, CancellationToken ct)
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
