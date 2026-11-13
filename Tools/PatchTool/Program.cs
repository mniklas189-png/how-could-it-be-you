using System.Diagnostics;
using System.IO.Compression;
using System.Net.Http.Headers;
using System.Text.Json;
using System.Text.Json.Serialization;
using HowCouldItBeYou.Launcher.Patching;

namespace HowCouldItBeYou.PatchTool;

/// <summary>
/// PatchTool make ...      see MakeCommand.cs (writes the manifest and chunk pack of a release)
/// PatchTool measure &lt;old folder|zip&gt; &lt;new folder|zip&gt; [--quality 0-11]
///   Prints how much a launcher with the old package would download to get the new one with chunks only.
/// PatchTool verify --package &lt;folder|zip&gt; --manifest &lt;file&gt;
///   Checks that a manifest describes exactly this package.
/// PatchTool diff &lt;old&gt; &lt;new&gt; &lt;patch&gt; / apply &lt;old&gt; &lt;patch&gt; &lt;out&gt; / selftest
///   Binary patches of single files, for testing.
/// </summary>
internal static class Program
{
    private const string DefaultRepo = "mniklas189-png/how-could-it-be-you";

    private static int Main(string[] args)
    {
        try
        {
            if (args.Length == 0)
            {
                Console.Error.WriteLine("Usage: PatchTool make|measure|verify ... (see Program.cs)");
                return 2;
            }
            var options = ParseOptions(args.Skip(1), out var positional);
            return args[0] switch
            {
                "make" => MakeCommand.Run(options, DefaultRepo),
                "measure" => Measure(positional, options),
                "verify" => Verify(options),
                "diff" => DiffFiles(positional, options),
                "apply" => ApplyPatch(positional),
                "selftest" => SelfTest(),
                _ => throw new ArgumentException($"Unknown command {args[0]}"),
            };
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("[PatchTool] FEHLER: " + ex.Message);
            return 1;
        }
    }

    private static Dictionary<string, string> ParseOptions(IEnumerable<string> args, out List<string> positional)
    {
        var options = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        positional = [];
        string? key = null;
        foreach (var arg in args)
        {
            if (key is not null)
            {
                options[key] = arg;
                key = null;
            }
            else if (arg.StartsWith("--", StringComparison.Ordinal))
            {
                key = arg[2..];
            }
            else
            {
                positional.Add(arg);
            }
        }
        if (key is not null)
        {
            throw new ArgumentException($"--{key} needs a value");
        }
        return options;
    }

    internal static string Require(Dictionary<string, string> options, string key) =>
        options.TryGetValue(key, out var value) ? value : throw new ArgumentException($"--{key} is missing");

    // ---------------------------------------------------------------- measure

    private static int Measure(List<string> positional, Dictionary<string, string> options)
    {
        if (positional.Count != 2)
        {
            throw new ArgumentException("measure <old> <new>");
        }
        var quality = int.Parse(options.GetValueOrDefault("quality", "9"));
        var watch = Stopwatch.StartNew();
        var oldChunks = new HashSet<ChunkHash>();
        long oldBytes = 0;
        using (var oldSource = PackageSource.Open(positional[0]))
        {
            foreach (var file in oldSource.Files)
            {
                var data = oldSource.Read(file);
                oldBytes += data.Length;
                foreach (var chunk in CutBytes(data))
                {
                    oldChunks.Add(chunk.Hash);
                }
            }
        }
        Console.WriteLine($"old: {Mb(oldBytes)}, {oldChunks.Count} unique chunks ({watch.Elapsed.TotalSeconds:0.0} s)");

        long newBytes = 0, freshRaw = 0, freshBrotli = 0, freshDeflate = 0;
        var seen = new HashSet<ChunkHash>();
        var rows = new List<(string Path, long Size, long Fresh, long Brotli)>();
        var compressWatch = new Stopwatch();
        using (var newSource = PackageSource.Open(positional[1]))
        {
            foreach (var file in newSource.Files)
            {
                var data = newSource.Read(file);
                newBytes += data.Length;
                var fresh = CutBytes(data).Where(c => !oldChunks.Contains(c.Hash) && seen.Add(c.Hash)).ToArray();
                long raw = fresh.Sum(c => (long)c.Length), brotli = 0, deflate = 0;
                compressWatch.Start();
                Parallel.ForEach(fresh, c =>
                {
                    var span = data.AsSpan((int)c.Offset, c.Length);
                    Interlocked.Add(ref brotli, Compress(span, quality).Length);
                    Interlocked.Add(ref deflate, Deflate(span));
                });
                compressWatch.Stop();
                freshRaw += raw;
                freshBrotli += brotli;
                freshDeflate += deflate;
                rows.Add((file.Path, data.Length, raw, brotli));
            }
        }
        foreach (var row in rows.Where(r => r.Fresh > 0).OrderByDescending(r => r.Fresh).Take(25))
        {
            Console.WriteLine($"  {Mb(row.Fresh),10} new of {Mb(row.Size),10} -> {Mb(row.Brotli),10}  {row.Path}");
        }
        Console.WriteLine($"new: {Mb(newBytes)}; chunks not in old: {Mb(freshRaw)} raw, {Mb(freshBrotli)} brotli q{quality}, {Mb(freshDeflate)} deflate");
        Console.WriteLine($"compression time {compressWatch.Elapsed.TotalSeconds:0.0} s, total {watch.Elapsed.TotalSeconds:0.0} s");
        return 0;
    }

    // ---------------------------------------------------------------- verify

    private static int Verify(Dictionary<string, string> options)
    {
        var manifest = PatchManifest.Parse(File.ReadAllText(Require(options, "manifest")));
        manifest.Validate();
        using var source = PackageSource.Open(Require(options, "package"));
        var files = source.Files.ToDictionary(f => f.Path, StringComparer.Ordinal);
        var errors = 0;
        foreach (var entry in manifest.Files)
        {
            if (!files.Remove(entry.Path, out var file))
            {
                Console.WriteLine($"missing in package: {entry.Path}");
                errors++;
                continue;
            }
            var hashes = CutBytes(source.Read(file)).Select(c => c.Hash.ToString()).ToList();
            if (!hashes.SequenceEqual(entry.Chunks.Select(i => manifest.Chunks[i].Hash)))
            {
                Console.WriteLine($"different: {entry.Path}");
                errors++;
            }
        }
        foreach (var extra in files.Keys)
        {
            Console.WriteLine($"not in manifest: {extra}");
            errors++;
        }
        Console.WriteLine(errors == 0 ? $"OK: {manifest.Files.Count} files match" : $"{errors} problems");
        return errors == 0 ? 0 : 3;
    }

    // ---------------------------------------------------------------- diff / apply (single files, for testing)

    private static int DiffFiles(List<string> positional, Dictionary<string, string> options)
    {
        if (positional.Count != 3)
        {
            throw new ArgumentException("diff <old> <new> <patch>");
        }
        var quality = int.Parse(options.GetValueOrDefault("quality", "9"));
        var watch = Stopwatch.StartNew();
        var patch = BinaryDiff.Create(File.ReadAllBytes(positional[0]), File.ReadAllBytes(positional[1]), quality, Console.WriteLine);
        File.WriteAllBytes(positional[2], patch);
        Console.WriteLine($"patch {Mb(patch.Length)} in {watch.Elapsed.TotalSeconds:0.0} s");
        return 0;
    }

    private static int ApplyPatch(List<string> positional)
    {
        if (positional.Count != 3)
        {
            throw new ArgumentException("apply <old> <patch> <out>");
        }
        var watch = Stopwatch.StartNew();
        using (var old = new FileStream(positional[0], FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 16))
        using (var output = File.Create(positional[2]))
        {
            BinaryPatch.Apply(File.ReadAllBytes(positional[1]), old, output);
        }
        Console.WriteLine($"applied in {watch.Elapsed.TotalSeconds:0.0} s");
        return 0;
    }

    /// <summary>Suffix array against a plain sort, and diff + apply round trips on random and repetitive data.</summary>
    private static int SelfTest()
    {
        var random = new Random(1234);
        for (var round = 0; round < 400; round++)
        {
            var length = random.Next(0, 300);
            var alphabet = round % 3 == 0 ? 2 : round % 3 == 1 ? 4 : 256;
            var data = new byte[length];
            for (var i = 0; i < length; i++)
            {
                data[i] = (byte)random.Next(alphabet);
            }
            var expected = Enumerable.Range(0, length + 1).ToArray();
            Array.Sort(expected, (a, b) => data.AsSpan(a).SequenceCompareTo(data.AsSpan(b)));
            if (!SuffixArray.Build(data).SequenceEqual(expected))
            {
                Console.WriteLine($"suffix array wrong for round {round}: len {length} data {string.Join(",", data)}");
                Console.WriteLine("got " + string.Join(",", SuffixArray.Build(data)));
                Console.WriteLine("exp " + string.Join(",", expected));
                return 4;
            }
        }
        for (var round = 0; round < 60; round++)
        {
            var oldData = new byte[random.Next(0, 200_000)];
            random.NextBytes(oldData);
            if (round % 4 == 0)
            {
                Array.Fill(oldData, (byte)7, 0, oldData.Length / 2);
            }
            // New = old with insertions, deletions and sparse byte changes (like shifted addresses)
            var newData = new List<byte>(oldData);
            for (var edit = random.Next(0, 30); edit > 0 && newData.Count > 10; edit--)
            {
                var at = random.Next(newData.Count);
                switch (random.Next(3))
                {
                    case 0: newData.InsertRange(at, Enumerable.Range(0, random.Next(1, 3000)).Select(_ => (byte)random.Next(256))); break;
                    case 1: newData.RemoveRange(at, Math.Min(random.Next(1, 3000), newData.Count - at)); break;
                    default: for (var i = at; i < newData.Count; i += 37) { newData[i] ^= 0x5A; } break;
                }
            }
            var target = newData.ToArray();
            var patch = BinaryDiff.Create(oldData, target, 5);
            using var output = new MemoryStream();
            BinaryPatch.Apply(patch, new MemoryStream(oldData), output);
            if (!output.ToArray().AsSpan().SequenceEqual(target))
            {
                Console.WriteLine($"diff/apply wrong for round {round}");
                return 5;
            }
        }
        Console.WriteLine("selftest OK");
        return 0;
    }

    // ---------------------------------------------------------------- helpers

    internal static List<FileChunk> CutBytes(byte[] data)
    {
        var chunks = new List<FileChunk>();
        using var stream = new MemoryStream(data, writable: false);
        ContentChunker.Split(stream, null, chunks.Add);
        return chunks;
    }

    /// <summary>Brotli if that is smaller, otherwise the raw bytes (the launcher tells them apart by the stored size).</summary>
    internal static byte[] Compress(ReadOnlySpan<byte> data, int quality)
    {
        var buffer = new byte[BrotliEncoder.GetMaxCompressedLength(data.Length)];
        if (BrotliEncoder.TryCompress(data, buffer, out var written, quality, 22) && written < data.Length)
        {
            return buffer.AsSpan(0, written).ToArray();
        }
        return data.ToArray();
    }

    private static long Deflate(ReadOnlySpan<byte> data)
    {
        using var output = new MemoryStream();
        using (var deflate = new DeflateStream(output, CompressionLevel.Optimal, leaveOpen: true))
        {
            deflate.Write(data);
        }
        return Math.Min(output.Length, data.Length);
    }

    private static string Mb(long bytes) => $"{bytes / 1048576.0:0.0} MB";
}

/// <summary>A packaged game as a folder or as the release zip. "Saved" folders (logs of test runs) are left out.</summary>
internal sealed class PackageSource : IDisposable
{
    public sealed record Entry(string Path, long Size, string? FullPath, ZipArchiveEntry? ZipEntry);

    private readonly ZipArchive? _zip;
    public List<Entry> Files { get; }

    private PackageSource(ZipArchive? zip, List<Entry> files)
    {
        _zip = zip;
        Files = files;
    }

    public static PackageSource Open(string path)
    {
        path = System.IO.Path.GetFullPath(path);
        List<Entry> files;
        ZipArchive? zip = null;
        if (Directory.Exists(path))
        {
            files = Directory.EnumerateFiles(path, "*", SearchOption.AllDirectories)
                .Select(full => new Entry(System.IO.Path.GetRelativePath(path, full).Replace('\\', '/'), new FileInfo(full).Length, full, null))
                .ToList();
        }
        else if (File.Exists(path) && path.EndsWith(".zip", StringComparison.OrdinalIgnoreCase))
        {
            zip = ZipFile.OpenRead(path);
            files = zip.Entries.Where(e => !e.FullName.EndsWith('/') && !e.FullName.EndsWith('\\'))
                .Select(e => new Entry(e.FullName.Replace('\\', '/'), e.Length, null, e))
                .ToList();
        }
        else
        {
            throw new FileNotFoundException($"Package not found: {path}");
        }
        files = files.Where(f => !PatchManifest.IsLocalState(f.Path) && PatchManifest.PathIsSafe(f.Path))
                     .OrderBy(f => f.Path, StringComparer.Ordinal).ToList();
        if (files.Count == 0)
        {
            throw new InvalidDataException($"Package is empty: {path}");
        }
        return new PackageSource(zip, files);
    }

    public Entry? Find(string path) => Files.FirstOrDefault(f => f.Path == path);

    public byte[] Read(Entry entry)
    {
        if (entry.FullPath is not null)
        {
            return File.ReadAllBytes(entry.FullPath);
        }
        using var stream = entry.ZipEntry!.Open();
        var data = new byte[entry.ZipEntry.Length];
        stream.ReadExactly(data);
        return data;
    }

    public void Dispose() => _zip?.Dispose();
}
