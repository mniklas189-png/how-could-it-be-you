using System.Diagnostics;
using System.IO.Compression;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.Json.Serialization;
using HowCouldItBeYou.Launcher.Patching;

namespace HowCouldItBeYou.PatchTool;

/// <summary>
/// PatchTool make --package &lt;zip|folder&gt; --platform Win64|Linux --tag v1.2.3 --out &lt;folder&gt;
///   [--base auto|none|&lt;manifest.json&gt;]  manifest of the previous release (auto: newest GitHub release with one)
///   [--base-package &lt;zip|folder&gt;]      previous release's package, for binary patches of the program files
///                                        (default: &lt;out&gt;/HowCouldItBeYou-&lt;platform&gt;-&lt;base tag&gt;.zip if it exists)
///   [--repo owner/name] [--quality 0-11] [--max-patches 8]
/// Writes HowCouldItBeYou-&lt;platform&gt;-&lt;tag&gt;.manifest.json and, if anything is new, ...-&lt;tag&gt;.chunks
/// (only the chunks and patches the base release does not have). Both go into the GitHub release next to the zip.
/// </summary>
internal static class MakeCommand
{
    /// <summary>A file gets a binary patch only when its new chunks are at least this big ...</summary>
    private const long PatchThreshold = 1 << 20;

    /// <summary>... and the patch is clearly smaller than those chunks.</summary>
    private const double PatchWorthIt = 0.8;

    public static int Run(Dictionary<string, string> options, string defaultRepo)
    {
        var platform = Program.Require(options, "platform");
        var tag = Program.Require(options, "tag");
        var outDir = Path.GetFullPath(Program.Require(options, "out"));
        var repo = options.GetValueOrDefault("repo", defaultRepo);
        var quality = int.Parse(options.GetValueOrDefault("quality", "9"));
        var maxPatches = int.Parse(options.GetValueOrDefault("max-patches", "8"));
        var baseArg = options.GetValueOrDefault("base", "auto");
        Directory.CreateDirectory(outDir);

        var assetBase = PatchManifest.AssetBase(platform, tag);
        var manifestPath = Path.Combine(outDir, assetBase + PatchManifest.FileSuffix);
        var packName = assetBase + PatchManifest.PackSuffix;
        var packPath = Path.Combine(outDir, packName);

        // ---- Base: what players of the previous release already have and where its chunks lie
        var baseManifest = baseArg switch
        {
            "none" => null,
            "auto" => FetchBaseManifest(repo, platform, tag),
            _ => PatchManifest.Parse(File.ReadAllText(baseArg)),
        };
        if (baseManifest is not null)
        {
            baseManifest.Validate();
            if (!string.Equals(baseManifest.Platform, platform, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException($"Base manifest is for {baseManifest.Platform}, not {platform}");
            }
            if (string.Equals(baseManifest.Tag, tag, StringComparison.OrdinalIgnoreCase))
            {
                throw new InvalidDataException($"Base manifest is the same version {tag}");
            }
            Log($"Basis: {baseManifest.Tag} ({baseManifest.Chunks.Count} Chunks in {baseManifest.Packs.Count} Paketen)");
        }
        else
        {
            Log("Kein Basis-Manifest: alle Chunks kommen in das neue Paket (einmalig gross).");
        }

        var basePackagePath = options.GetValueOrDefault("base-package");
        if (basePackagePath == "none")
        {
            basePackagePath = null;
        }
        else if (basePackagePath is null && baseManifest is not null)
        {
            var guess = Path.Combine(outDir, PatchManifest.AssetBase(platform, baseManifest.Tag) + ".zip");
            basePackagePath = File.Exists(guess) ? guess : null;
        }
        else if (basePackagePath is null)
        {
            // No manifest yet (first release with small updates): the newest older zip next to the new one
            basePackagePath = NewestOlderZip(outDir, platform, tag);
        }
        using var basePackage = basePackagePath is null ? null : PackageSource.Open(basePackagePath);
        Log(basePackage is null
            ? "Kein Paket der Vorversion gefunden: keine Binaer-Patches (nur Chunks)."
            : $"Vorversion fuer Binaer-Patches: {basePackagePath}");

        var known = new Dictionary<ChunkHash, PatchChunk>();
        var baseFiles = new Dictionary<string, PatchFile>(StringComparer.Ordinal);
        if (baseManifest is not null)
        {
            foreach (var chunk in baseManifest.Chunks)
            {
                known.TryAdd(ChunkHash.Parse(chunk.Hash), chunk);
            }
            foreach (var file in baseManifest.Files)
            {
                baseFiles[file.Path] = file;
            }
        }

        // ---- New manifest
        var manifest = new PatchManifest { Platform = platform, Tag = tag };
        var packIndexByKey = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
        int PackIndex(string packTag, string asset, long size)
        {
            var key = packTag + "/" + asset;
            if (!packIndexByKey.TryGetValue(key, out var index))
            {
                index = manifest.Packs.Count;
                manifest.Packs.Add(new PatchPack { Tag = packTag, Asset = asset, Size = size });
                packIndexByKey[key] = index;
            }
            return index;
        }
        int BasePack(int index) => PackIndex(baseManifest!.Packs[index].Tag, baseManifest.Packs[index].Asset, baseManifest.Packs[index].Size);
        var ownPack = -1;
        int OwnPack() => ownPack >= 0 ? ownPack : ownPack = PackIndex(tag, packName, 0);

        var chunkIndex = new Dictionary<ChunkHash, int>();
        var watch = Stopwatch.StartNew();
        long totalBytes = 0, newRaw = 0, newStored = 0, patchBytes = 0;
        var tempPack = packPath + ".tmp";
        using (var source = PackageSource.Open(Program.Require(options, "package")))
        using (var pack = File.Create(tempPack))
        {
            foreach (var file in source.Files)
            {
                var data = source.Read(file);
                var chunks = Program.CutBytes(data);
                var entry = new PatchFile { Path = file.Path, Size = data.Length };
                totalBytes += data.Length;

                // Chunks new in this release, compressed in parallel, then written in file order
                var fresh = chunks.Where(c => !chunkIndex.ContainsKey(c.Hash) && !known.ContainsKey(c.Hash))
                                  .DistinctBy(c => c.Hash).ToArray();
                var compressed = new byte[fresh.Length][];
                Parallel.For(0, fresh.Length, i => compressed[i] = Program.Compress(data.AsSpan((int)fresh[i].Offset, fresh[i].Length), quality));
                long fileFresh = 0;
                for (var i = 0; i < fresh.Length; i++)
                {
                    var stored = compressed[i];
                    chunkIndex[fresh[i].Hash] = manifest.Chunks.Count;
                    manifest.Chunks.Add(new PatchChunk
                    {
                        Hash = fresh[i].Hash.ToString(), Size = fresh[i].Length, Pack = OwnPack(), Offset = pack.Position, Stored = stored.Length,
                    });
                    pack.Write(stored);
                    newRaw += fresh[i].Length;
                    fileFresh += stored.Length;
                }
                newStored += fileFresh;

                foreach (var chunk in chunks)
                {
                    if (!chunkIndex.TryGetValue(chunk.Hash, out var index))
                    {
                        var old = known[chunk.Hash];
                        index = manifest.Chunks.Count;
                        chunkIndex[chunk.Hash] = index;
                        manifest.Chunks.Add(new PatchChunk
                        {
                            Hash = old.Hash, Size = old.Size, Pack = BasePack(old.Pack), Offset = old.Offset, Stored = old.Stored,
                        });
                    }
                    entry.Chunks.Add(index);
                }

                // Binary patches for files whose chunks are nearly all new (program, debug files)
                var id = PatchManifest.FileId(chunks.Select(c => c.Hash));
                baseFiles.TryGetValue(file.Path, out var baseFile);
                var baseId = baseFile is null ? null : baseManifest!.FileId(baseFile);
                var carried = baseFile?.Patches ?? [];
                List<PatchEdge>? edges = null;
                if (baseId == id)
                {
                    edges = carried.Select(e => Remap(e, BasePack(e.Pack))).ToList();
                }
                else if (fileFresh >= PatchThreshold && basePackage?.Find(file.Path) is { } oldEntry)
                {
                    var oldData = basePackage.Read(oldEntry);
                    var oldId = PatchManifest.FileId(Program.CutBytes(oldData).Select(c => c.Hash));
                    if (baseId is not null && oldId != baseId)
                    {
                        Log($"  {file.Path}: Paket der Vorversion passt nicht zu {baseManifest!.Tag}, kein Patch");
                    }
                    else if (oldId != id)
                    {
                        Log($"  {file.Path}: Binaer-Patch ({fileFresh / 1048576.0:0.0} MB Chunks) ...");
                        var patch = BinaryDiff.Create(oldData, data, quality, line => Log("    " + line));
                        if (patch.Length < fileFresh * PatchWorthIt)
                        {
                            edges = carried.Select(e => Remap(e, BasePack(e.Pack))).ToList();
                            edges.Add(new PatchEdge { From = oldId, To = id, Pack = OwnPack(), Offset = pack.Position, Stored = patch.Length });
                            pack.Write(patch);
                            patchBytes += patch.Length;
                            Log($"    -> {patch.Length / 1048576.0:0.0} MB statt {fileFresh / 1048576.0:0.0} MB");
                        }
                        else
                        {
                            Log($"    -> {patch.Length / 1048576.0:0.0} MB, lohnt nicht");
                        }
                    }
                }
                if (edges is { Count: > 0 })
                {
                    entry.Patches = edges.TakeLast(maxPatches).ToList();
                }
                manifest.Files.Add(entry);
            }
            if (ownPack >= 0)
            {
                manifest.Packs[ownPack].Size = pack.Length;
            }
        }
        if (ownPack >= 0)
        {
            File.Move(tempPack, packPath, overwrite: true);
        }
        else
        {
            File.Delete(tempPack);
            File.Delete(packPath);
        }

        manifest.Validate();
        File.WriteAllText(manifestPath, manifest.ToJson());

        Log($"{manifest.Files.Count} Dateien, {totalBytes / 1048576.0:0.0} MB, {manifest.Chunks.Count} Chunks ({watch.Elapsed.TotalSeconds:0} s)");
        Log($"Neu in {tag}: {newRaw / 1048576.0:0.0} MB Chunks -> {newStored / 1048576.0:0.0} MB, Binaer-Patches {patchBytes / 1048576.0:0.0} MB");
        Log($"Manifest: {manifestPath}");
        Log(ownPack >= 0 ? $"Paket:    {packPath} ({new FileInfo(packPath).Length / 1048576.0:0.0} MB)" : "Kein neues Paket noetig (nichts geaendert).");
        return 0;
    }

    private static string? NewestOlderZip(string folder, string platform, string tag)
    {
        static Version? Parse(string text) => Version.TryParse(text.TrimStart('v', 'V'), out var version) ? version : null;
        var current = Parse(tag);
        var prefix = $"HowCouldItBeYou-{platform}-";
        return Directory.EnumerateFiles(folder, prefix + "*.zip")
            .Select(path => (Path: path, Version: Parse(Path.GetFileNameWithoutExtension(path)[prefix.Length..])))
            .Where(z => z.Version is not null && (current is null || z.Version < current))
            .OrderByDescending(z => z.Version)
            .Select(z => z.Path)
            .FirstOrDefault();
    }

    private static PatchEdge Remap(PatchEdge edge, int pack) =>
        new() { From = edge.From, To = edge.To, Pack = pack, Offset = edge.Offset, Stored = edge.Stored };

    private static void Log(string line) => Console.WriteLine("[PatchTool] " + line);

    private static PatchManifest? FetchBaseManifest(string repo, string platform, string tag)
    {
        using var http = new HttpClient { Timeout = TimeSpan.FromMinutes(2) };
        http.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("HowCouldItBeYou-PatchTool", "1.0"));
        var token = Environment.GetEnvironmentVariable("GH_TOKEN") ?? Environment.GetEnvironmentVariable("GITHUB_TOKEN");
        if (!string.IsNullOrWhiteSpace(token))
        {
            http.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Bearer", token);
        }
        var json = http.GetStringAsync($"https://api.github.com/repos/{repo}/releases?per_page=50").GetAwaiter().GetResult();
        var releases = JsonSerializer.Deserialize<List<GhRelease>>(json) ?? [];
        var prefix = $"HowCouldItBeYou-{platform}-";
        foreach (var release in releases.Where(r => !r.Draft && !r.Prerelease && !string.Equals(r.TagName, tag, StringComparison.OrdinalIgnoreCase))
                                        .OrderByDescending(r => r.PublishedAt))
        {
            var asset = release.Assets.FirstOrDefault(a => a.Name.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) &&
                                                          a.Name.EndsWith(PatchManifest.FileSuffix, StringComparison.OrdinalIgnoreCase));
            if (asset is null)
            {
                continue;
            }
            Log($"Lade Basis-Manifest {asset.Name} ({release.TagName}) ...");
            return PatchManifest.Parse(http.GetStringAsync(asset.BrowserDownloadUrl).GetAwaiter().GetResult());
        }
        return null;
    }

    private sealed class GhRelease
    {
        [JsonPropertyName("tag_name")] public string TagName { get; set; } = "";
        [JsonPropertyName("draft")] public bool Draft { get; set; }
        [JsonPropertyName("prerelease")] public bool Prerelease { get; set; }
        [JsonPropertyName("published_at")] public DateTimeOffset PublishedAt { get; set; }
        [JsonPropertyName("assets")] public List<GhAsset> Assets { get; set; } = [];
    }

    private sealed class GhAsset
    {
        [JsonPropertyName("name")] public string Name { get; set; } = "";
        [JsonPropertyName("browser_download_url")] public string BrowserDownloadUrl { get; set; } = "";
    }
}
