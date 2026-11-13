using System.Collections.Concurrent;
using System.IO.Compression;
using System.Net;
using System.Net.Http.Headers;
using System.Text;

namespace HowCouldItBeYou.Launcher.Patching;

/// <summary>The small update did not work out before anything was changed: the caller downloads the whole zip instead.</summary>
public sealed class PatchFallbackException(string reason, Exception? inner = null) : Exception(reason, inner);

/// <summary>What an update from the installed files to a manifest needs.</summary>
public sealed class PatchPlan
{
    public required PatchManifest Manifest { get; init; }
    /// <summary>Bytes to download (including the small gaps between neighbouring chunks).</summary>
    public long DownloadBytes { get; init; }
    public int ChangedFiles => Actions.Count;
    internal List<FileAction> Actions { get; init; } = [];
    internal List<string> Obsolete { get; init; } = [];
    internal Dictionary<ChunkHash, LocalChunk> Local { get; init; } = [];
    internal ChunkHash[] Hashes { get; init; } = [];
    internal List<DownloadRange> Ranges { get; init; } = [];
    internal List<string> Notes { get; init; } = [];
}

internal sealed record FileAction(PatchFile File, List<PatchEdge>? Chain);

internal readonly record struct LocalChunk(string Path, long Offset, int Length);

/// <summary>A piece of a pack: a chunk or a binary patch.</summary>
internal readonly record struct PackItem(int Pack, long Offset, long Stored);

/// <summary>One HTTP range request; Items are the pieces inside it, TempOffset is where it lands in the download file.</summary>
internal sealed class DownloadRange
{
    public required int Pack { get; init; }
    public required long Start { get; init; }
    public required long End { get; set; }
    public required List<PackItem> Items { get; init; }
    public long TempOffset { get; set; }
}

/// <summary>
/// Updates an installed game to a release by downloading only what changed (see <see cref="PatchManifest"/>):
/// cuts the installed files into chunks, reuses every chunk the new version still has, follows binary patch chains
/// for the program files and fetches the rest from the chunk packs with HTTP range requests. New and changed files
/// are built next to the game first; only then are they moved in (the old ones go to update.previous until the end),
/// so a failed or cancelled update keeps the old version.
/// </summary>
public sealed class PatchUpdater
{
    /// <summary>Neighbouring pieces closer than this are fetched in one request (the gap is downloaded and thrown away).</summary>
    private const long MergeGap = 256 * 1024;

    private readonly HttpClient _http;
    private readonly string _install;
    private readonly string _work;
    private readonly Func<PatchPack, string> _packUrl;
    private readonly StringComparer _pathComparer = OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal;

    /// <summary>Chunks of installed files, kept while size and time stay the same (planning twice reads the files once).</summary>
    private readonly ConcurrentDictionary<string, (long Size, DateTime Time, List<FileChunk> Chunks)> _index = new();

    public PatchUpdater(HttpClient http, string installPath, string workDirectory, Func<PatchPack, string> packUrl)
    {
        _http = http;
        _install = installPath;
        _work = workDirectory;
        _packUrl = packUrl;
    }

    public static string GitHubPackUrl(string repository, PatchPack pack) =>
        $"https://github.com/{repository}/releases/download/{Uri.EscapeDataString(pack.Tag)}/{Uri.EscapeDataString(pack.Asset)}";

    private string TempDownload => Path.Combine(_work, "update.download");
    private string Staging => Path.Combine(_work, "update.staging");
    private string Previous => Path.Combine(_work, "update.previous");

    public async Task<PatchManifest> LoadManifestAsync(string url, CancellationToken ct)
    {
        try
        {
            await using var stream = await _http.GetStreamAsync(url, ct);
            var manifest = PatchManifest.Parse(stream);
            manifest.Validate();
            return manifest;
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            throw new PatchFallbackException("manifest: " + ex.Message, ex);
        }
    }

    /// <summary>Compares the installed files with the manifest. Reads every installed file (a few seconds).</summary>
    public PatchPlan Plan(PatchManifest manifest, IProgress<double>? progress, CancellationToken ct)
    {
        try
        {
            return PlanInner(manifest, progress, ct);
        }
        catch (Exception ex) when (ex is not OperationCanceledException and not PatchFallbackException)
        {
            throw new PatchFallbackException("plan: " + ex.Message, ex);
        }
    }

    private PatchPlan PlanInner(PatchManifest manifest, IProgress<double>? progress, CancellationToken ct)
    {
        // 1) Installed files → chunks
        var files = Directory.Exists(_install)
            ? Directory.EnumerateFiles(_install, "*", SearchOption.AllDirectories)
                .Select(full => (Full: full, Rel: Path.GetRelativePath(_install, full).Replace('\\', '/')))
                .Where(f => !PatchManifest.IsLocalState(f.Rel))
                .Select(f => (f.Full, f.Rel, Info: new FileInfo(f.Full)))
                .ToList()
            : [];
        var totalBytes = Math.Max(1, files.Sum(f => f.Info.Length));
        long readBytes = 0;
        var local = new Dictionary<string, List<FileChunk>>(_pathComparer);
        var have = new Dictionary<ChunkHash, LocalChunk>();
        foreach (var (full, rel, info) in files)
        {
            ct.ThrowIfCancellationRequested();
            List<FileChunk> chunks;
            if (_index.TryGetValue(full, out var cached) && cached.Size == info.Length && cached.Time == info.LastWriteTimeUtc)
            {
                chunks = cached.Chunks;
                readBytes += info.Length;
            }
            else
            {
                chunks = ContentChunker.SplitFile(full, read =>
                {
                    readBytes += read;
                    progress?.Report((double)readBytes / totalBytes);
                }, ct);
                _index[full] = (info.Length, info.LastWriteTimeUtc, chunks);
            }
            progress?.Report((double)readBytes / totalBytes);
            local[rel] = chunks;
            foreach (var chunk in chunks)
            {
                have.TryAdd(chunk.Hash, new LocalChunk(full, chunk.Offset, chunk.Length));
            }
        }

        // 2) Per file of the new version: unchanged, from chunks, or through binary patches
        var hashes = manifest.Chunks.Select(c => ChunkHash.Parse(c.Hash)).ToArray();
        var actions = new List<FileAction>();
        var neededChunks = new HashSet<int>();
        var neededPatches = new List<PatchEdge>();
        var notes = new List<string>();
        foreach (var file in manifest.Files)
        {
            local.TryGetValue(file.Path, out var mine);
            if (mine is not null && mine.Select(c => c.Hash).SequenceEqual(file.Chunks.Select(i => hashes[i])))
            {
                continue;
            }
            var missing = file.Chunks.Where(i => !have.ContainsKey(hashes[i])).Distinct().ToList();
            var chunkCost = missing.Sum(i => (long)manifest.Chunks[i].Stored);
            List<PatchEdge>? chain = null;
            if (mine is not null && file.Patches is { Count: > 0 } && chunkCost > 0)
            {
                chain = CheapestChain(file.Patches, PatchManifest.FileId(mine.Select(c => c.Hash)), manifest.FileId(file));
                if (chain is not null && chain.Sum(e => e.Stored) >= chunkCost)
                {
                    chain = null;
                }
            }
            actions.Add(new FileAction(file, chain));
            if (chain is not null)
            {
                neededPatches.AddRange(chain);
                notes.Add($"{file.Path}: {chain.Count} patch(es), {chain.Sum(e => e.Stored)} bytes instead of {chunkCost}");
            }
            else
            {
                neededChunks.UnionWith(missing);
                notes.Add($"{file.Path}: {missing.Count} chunks, {chunkCost} bytes");
            }
        }
        var wanted = new HashSet<string>(manifest.Files.Select(f => f.Path), _pathComparer);
        var obsolete = local.Keys.Where(p => !wanted.Contains(p)).ToList();

        // 3) Pieces to download, merged into few range requests per pack
        var items = neededChunks.Select(i => new PackItem(manifest.Chunks[i].Pack, manifest.Chunks[i].Offset, manifest.Chunks[i].Stored))
            .Concat(neededPatches.Select(e => new PackItem(e.Pack, e.Offset, e.Stored)))
            .Distinct()
            .OrderBy(i => i.Pack).ThenBy(i => i.Offset)
            .ToList();
        var ranges = new List<DownloadRange>();
        foreach (var item in items)
        {
            var last = ranges.Count > 0 ? ranges[^1] : null;
            if (last is not null && last.Pack == item.Pack && item.Offset <= last.End + MergeGap)
            {
                last.End = Math.Max(last.End, item.Offset + item.Stored);
                last.Items.Add(item);
            }
            else
            {
                ranges.Add(new DownloadRange { Pack = item.Pack, Start = item.Offset, End = item.Offset + item.Stored, Items = [item] });
            }
        }

        return new PatchPlan
        {
            Manifest = manifest,
            DownloadBytes = ranges.Sum(r => r.End - r.Start),
            Actions = actions,
            Obsolete = obsolete,
            Local = have,
            Hashes = hashes,
            Ranges = ranges,
            Notes = notes,
        };
    }

    /// <summary>Cheapest row of patches from one file id to another (a handful of edges: plain relaxation).</summary>
    private static List<PatchEdge>? CheapestChain(List<PatchEdge> edges, string from, string to)
    {
        if (from == to)
        {
            return null;
        }
        var best = new Dictionary<string, (long Cost, List<PatchEdge> Path)> { [from] = (0, []) };
        for (var round = 0; round < edges.Count; round++)
        {
            var changed = false;
            foreach (var edge in edges)
            {
                if (best.TryGetValue(edge.From, out var start) &&
                    (!best.TryGetValue(edge.To, out var known) || start.Cost + edge.Stored < known.Cost))
                {
                    best[edge.To] = (start.Cost + edge.Stored, [.. start.Path, edge]);
                    changed = true;
                }
            }
            if (!changed)
            {
                break;
            }
        }
        return best.TryGetValue(to, out var result) && result.Path.Count > 0 ? result.Path : null;
    }

    /// <summary>Downloads, builds and installs. Errors before the first file is moved become a <see cref="PatchFallbackException"/>.</summary>
    public async Task ApplyAsync(PatchPlan plan, string tag, IProgress<(Phrase Status, double? Fraction)> progress, CancellationToken ct)
    {
        TryDelete(TempDownload);
        TryDeleteDir(Staging);
        try
        {
            await DownloadAsync(plan, progress, ct);
            progress.Report((new("Baue die neuen Dateien zusammen ...", "Assembling the new files ..."), null));
            await Task.Run(() => Build(plan, progress, ct), ct);
        }
        catch (Exception ex) when (ex is not OperationCanceledException and not PatchFallbackException)
        {
            throw new PatchFallbackException(ex.Message, ex);
        }

        progress.Report((new("Installiere ...", "Installing ..."), null));
        ct.ThrowIfCancellationRequested();
        await Task.Run(() => Commit(plan, tag), CancellationToken.None);
        TryDelete(TempDownload);
        TryDeleteDir(Staging);
        GitHubUpdater.MarkExecutables(_install);
    }

    private async Task DownloadAsync(PatchPlan plan, IProgress<(Phrase Status, double? Fraction)> progress, CancellationToken ct)
    {
        var total = Math.Max(1, plan.DownloadBytes);
        long done = 0;
        var lastReport = DateTime.MinValue;
        void Report(bool force = false)
        {
            if (force || (DateTime.UtcNow - lastReport).TotalMilliseconds > 100)
            {
                lastReport = DateTime.UtcNow;
                var size = GitHubUpdater.FormatSize(done) + " / " + GitHubUpdater.FormatSize(plan.DownloadBytes);
                progress.Report((new($"Lade nur die Änderungen: {size}", $"Downloading only the changes: {size}"), (double)done / total));
            }
        }
        Report(force: true);

        await using var target = new FileStream(TempDownload, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 16, useAsync: true);
        var buffer = new byte[1 << 16];
        foreach (var range in plan.Ranges)
        {
            range.TempOffset = target.Position;
            var url = _packUrl(plan.Manifest.Packs[range.Pack]);
            for (var attempt = 1; ; attempt++)
            {
                var before = done;
                try
                {
                    using var request = new HttpRequestMessage(HttpMethod.Get, url);
                    request.Headers.Range = new RangeHeaderValue(range.Start, range.End - 1);
                    using var response = await _http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, ct);
                    if (response.StatusCode != HttpStatusCode.PartialContent)
                    {
                        // 200 would be the whole pack; 404 a missing release asset
                        throw new PatchFallbackException($"{url}: HTTP {(int)response.StatusCode} instead of 206");
                    }
                    var contentRange = response.Content.Headers.ContentRange;
                    if (contentRange?.From != range.Start)
                    {
                        throw new PatchFallbackException($"{url}: answer starts at {contentRange?.From}, not {range.Start}");
                    }
                    await using var source = await response.Content.ReadAsStreamAsync(ct);
                    var left = range.End - range.Start;
                    while (left > 0)
                    {
                        var read = await source.ReadAsync(buffer.AsMemory(0, (int)Math.Min(buffer.Length, left)), ct);
                        if (read == 0)
                        {
                            throw new IOException($"{url}: connection ended {left} bytes early");
                        }
                        await target.WriteAsync(buffer.AsMemory(0, read), ct);
                        left -= read;
                        done += read;
                        Report();
                    }
                    break;
                }
                catch (Exception ex) when (attempt < 3 && ex is HttpRequestException or IOException)
                {
                    // Try the same range again from its start
                    done = before;
                    target.SetLength(range.TempOffset);
                    target.Position = range.TempOffset;
                    await Task.Delay(1000 * attempt, ct);
                }
            }
        }
        Report(force: true);
    }

    /// <summary>Writes every new or changed file into update.staging and checks it chunk by chunk.</summary>
    private void Build(PatchPlan plan, IProgress<(Phrase Status, double? Fraction)> progress, CancellationToken ct)
    {
        var manifest = plan.Manifest;
        var where = new Dictionary<PackItem, long>();
        foreach (var range in plan.Ranges)
        {
            foreach (var item in range.Items)
            {
                where[item] = range.TempOffset + (item.Offset - range.Start);
            }
        }
        var total = Math.Max(1, plan.Actions.Sum(a => a.File.Size));
        long written = 0;
        var lastReport = DateTime.MinValue;
        void Report(long add)
        {
            written += add;
            if ((DateTime.UtcNow - lastReport).TotalMilliseconds > 100)
            {
                lastReport = DateTime.UtcNow;
                progress.Report((new("Baue die neuen Dateien zusammen ...", "Assembling the new files ..."), (double)written / total));
            }
        }

        using var download = new FileStream(TempDownload, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 16);
        byte[] ReadDownloaded(PackItem item)
        {
            var data = new byte[item.Stored];
            download.Position = where[item];
            download.ReadExactly(data);
            return data;
        }

        var sources = new Dictionary<string, FileStream>(StringComparer.Ordinal);
        try
        {
            var chunk = new byte[ContentChunker.MaxSize];
            foreach (var action in plan.Actions)
            {
                ct.ThrowIfCancellationRequested();
                var file = action.File;
                var staged = Path.Combine(Staging, file.Path);
                Directory.CreateDirectory(Path.GetDirectoryName(staged)!);

                if (action.Chain is not null)
                {
                    var current = Path.Combine(_install, file.Path);
                    for (var k = 0; k < action.Chain.Count; k++)
                    {
                        var edge = action.Chain[k];
                        var output = k == action.Chain.Count - 1 ? staged : staged + $".step{k}";
                        var patch = ReadDownloaded(new PackItem(edge.Pack, edge.Offset, edge.Stored));
                        using (var old = new FileStream(current, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 16))
                        using (var outStream = new FileStream(output, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 16))
                        {
                            BinaryPatch.Apply(patch, old, outStream, k == action.Chain.Count - 1 ? Report : null, ct);
                        }
                        if (current.StartsWith(Staging, StringComparison.Ordinal))
                        {
                            File.Delete(current);
                        }
                        current = output;
                    }
                    // The result must be exactly the file of the manifest
                    var result = ContentChunker.SplitFile(staged, null, ct).Select(c => c.Hash);
                    if (!result.SequenceEqual(file.Chunks.Select(i => plan.Hashes[i])))
                    {
                        throw new InvalidDataException($"{file.Path}: patched file does not match the manifest");
                    }
                    continue;
                }

                using var outFile = new FileStream(staged, FileMode.Create, FileAccess.Write, FileShare.None, 1 << 16);
                foreach (var index in file.Chunks)
                {
                    var hash = plan.Hashes[index];
                    var info = manifest.Chunks[index];
                    var bytes = chunk.AsSpan(0, info.Size);
                    if (plan.Local.TryGetValue(hash, out var mine))
                    {
                        if (!sources.TryGetValue(mine.Path, out var stream))
                        {
                            stream = new FileStream(mine.Path, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 16);
                            sources[mine.Path] = stream;
                        }
                        stream.Position = mine.Offset;
                        stream.ReadExactly(bytes);
                    }
                    else
                    {
                        var stored = ReadDownloaded(new PackItem(info.Pack, info.Offset, info.Stored));
                        if (info.Stored == info.Size)
                        {
                            stored.CopyTo(bytes);
                        }
                        else if (!BrotliDecoder.TryDecompress(stored, bytes, out var unpacked) || unpacked != info.Size)
                        {
                            throw new InvalidDataException($"{file.Path}: chunk {info.Hash} does not unpack");
                        }
                    }
                    if (ChunkHash.Of(bytes) != hash)
                    {
                        throw new InvalidDataException($"{file.Path}: chunk {info.Hash} is damaged");
                    }
                    outFile.Write(bytes);
                    Report(info.Size);
                }
            }
        }
        finally
        {
            foreach (var stream in sources.Values)
            {
                stream.Dispose();
            }
        }
    }

    /// <summary>Moves the built files into the game folder. On an error everything moved so far goes back.</summary>
    private void Commit(PatchPlan plan, string tag)
    {
        // Old files of the last update stay locked if that game was still running: then use a fresh folder
        var previous = Previous;
        TryDeleteDir(previous);
        if (Directory.Exists(previous))
        {
            previous += "." + DateTime.UtcNow.Ticks;
        }
        Directory.CreateDirectory(previous);
        var done = new List<(string Target, string? Backup, string? Staged)>();
        try
        {
            foreach (var action in plan.Actions)
            {
                var target = Path.Combine(_install, action.File.Path);
                var staged = Path.Combine(Staging, action.File.Path);
                string? backup = null;
                if (File.Exists(target))
                {
                    backup = Path.Combine(previous, action.File.Path);
                    Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
                    File.Move(target, backup);
                }
                done.Add((target, backup, null));
                Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                File.Move(staged, target);
                done[^1] = (target, backup, staged);
            }
            foreach (var path in plan.Obsolete)
            {
                var target = Path.Combine(_install, path);
                var backup = Path.Combine(previous, path);
                Directory.CreateDirectory(Path.GetDirectoryName(backup)!);
                File.Move(target, backup);
                done.Add((target, backup, null));
            }
            File.WriteAllText(Path.Combine(_install, "version.txt"), tag);
        }
        catch
        {
            for (var i = done.Count - 1; i >= 0; i--)
            {
                var (target, backup, staged) = done[i];
                try
                {
                    if (staged is not null && File.Exists(target))
                    {
                        File.Move(target, staged, overwrite: true);
                    }
                    if (backup is not null && File.Exists(backup))
                    {
                        File.Move(backup, target, overwrite: true);
                    }
                }
                catch
                {
                    // keep rolling back the rest
                }
            }
            throw;
        }
        TryDeleteDir(previous);
        foreach (var key in _index.Keys.Where(k => k.StartsWith(_install, StringComparison.Ordinal)).ToList())
        {
            _index.TryRemove(key, out _);
        }
    }

    /// <summary>Human-readable summary of a plan for update.log.</summary>
    public static string Describe(PatchPlan plan)
    {
        var text = new StringBuilder();
        text.AppendLine($"{plan.Manifest.Tag}: {plan.ChangedFiles} files change, {plan.Obsolete.Count} go, download {plan.DownloadBytes} bytes in {plan.Ranges.Count} requests");
        foreach (var note in plan.Notes)
        {
            text.AppendLine("  " + note);
        }
        foreach (var path in plan.Obsolete)
        {
            text.AppendLine("  remove " + path);
        }
        return text.ToString();
    }

    private static void TryDelete(string path)
    {
        try { if (File.Exists(path)) File.Delete(path); } catch { /* ignored */ }
    }

    private static void TryDeleteDir(string path)
    {
        try { if (Directory.Exists(path)) Directory.Delete(path, recursive: true); } catch { /* ignored */ }
    }
}
