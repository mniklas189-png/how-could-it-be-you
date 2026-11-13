using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace HowCouldItBeYou.Launcher.Patching;

/// <summary>
/// "HowCouldItBeYou-&lt;Win64|Linux&gt;-&lt;tag&gt;.manifest.json", uploaded next to the release zip by Tools/PatchTool.
/// Lists every file of the package as a row of chunks, and for every chunk the "chunk pack" it can be fetched
/// from. A pack ("...-&lt;tag&gt;.chunks") holds only the chunks that were new in its release, each compressed on its own,
/// so a launcher downloads just the chunks its installed version lacks (HTTP range requests) and copies the rest
/// from the files it already has. Packs of older releases stay referenced: never delete a release that has one.
///
/// The game's program and debug files change in nearly every chunk with each build (shifted addresses), so for
/// those a file can also list binary patches (<see cref="BinaryPatch"/>) from earlier versions of it: a launcher
/// whose file is one of those versions follows the cheapest chain of patches instead of fetching all chunks.
/// </summary>
public sealed class PatchManifest
{
    public const int CurrentFormat = 1;
    public const string FileSuffix = ".manifest.json";
    public const string PackSuffix = ".chunks";

    [JsonPropertyName("format")] public int Format { get; set; } = CurrentFormat;
    [JsonPropertyName("chunking")] public string Chunking { get; set; } = ContentChunker.Algorithm;
    [JsonPropertyName("platform")] public string Platform { get; set; } = "";
    [JsonPropertyName("tag")] public string Tag { get; set; } = "";
    [JsonPropertyName("packs")] public List<PatchPack> Packs { get; set; } = [];
    [JsonPropertyName("chunks")] public List<PatchChunk> Chunks { get; set; } = [];
    [JsonPropertyName("files")] public List<PatchFile> Files { get; set; } = [];

    /// <summary>Asset name prefix of one platform's files in a release, e.g. "HowCouldItBeYou-Win64-v0.12.1".</summary>
    public static string AssetBase(string platform, string tag) => $"HowCouldItBeYou-{platform}-{tag}";

    private static readonly JsonSerializerOptions Options = new()
    {
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    public static PatchManifest Parse(Stream json) =>
        JsonSerializer.Deserialize<PatchManifest>(json, Options) ?? throw new InvalidDataException("Empty manifest");

    public static PatchManifest Parse(string json) =>
        JsonSerializer.Deserialize<PatchManifest>(json, Options) ?? throw new InvalidDataException("Empty manifest");

    public string ToJson() => JsonSerializer.Serialize(this, Options);

    /// <summary>Content of a file as one number: hash over its chunk hashes (equal content = equal id).</summary>
    public static string FileId(IEnumerable<ChunkHash> chunks)
    {
        using var sha = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        Span<byte> pair = stackalloc byte[16];
        foreach (var chunk in chunks)
        {
            BinaryPrimitives.WriteUInt64BigEndian(pair, chunk.Hi);
            BinaryPrimitives.WriteUInt64BigEndian(pair[8..], chunk.Lo);
            sha.AppendData(pair);
        }
        return Convert.ToHexStringLower(sha.GetHashAndReset().AsSpan(0, 16));
    }

    public string FileId(PatchFile file) => FileId(file.Chunks.Select(i => ChunkHash.Parse(Chunks[i].Hash)));

    /// <summary>Throws when the launcher cannot use this manifest (newer format, other chunking, broken indices).</summary>
    public void Validate()
    {
        if (Format != CurrentFormat)
        {
            throw new InvalidDataException($"Manifest format {Format} is not supported (expected {CurrentFormat})");
        }
        if (Chunking != ContentChunker.Algorithm)
        {
            throw new InvalidDataException($"Manifest chunking \"{Chunking}\" is not supported");
        }
        bool InPack(int pack, long offset, long stored) =>
            pack >= 0 && pack < Packs.Count && offset >= 0 && stored > 0 && offset + stored <= Packs[pack].Size;
        foreach (var pack in Packs)
        {
            if (string.IsNullOrEmpty(pack.Tag) || string.IsNullOrEmpty(pack.Asset) || pack.Asset.Contains('/') || pack.Tag.Contains('/'))
            {
                throw new InvalidDataException("Manifest pack without a proper tag or asset name");
            }
        }
        foreach (var chunk in Chunks)
        {
            if (!InPack(chunk.Pack, chunk.Offset, chunk.Stored) || chunk.Size <= 0 || chunk.Size > ContentChunker.MaxSize || chunk.Stored > chunk.Size)
            {
                throw new InvalidDataException($"Manifest chunk {chunk.Hash} is out of range");
            }
            ChunkHash.Parse(chunk.Hash);
        }
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var file in Files)
        {
            if (!PathIsSafe(file.Path) || IsLocalState(file.Path) || !paths.Add(file.Path))
            {
                throw new InvalidDataException($"Manifest path is not allowed: {file.Path}");
            }
            long size = 0;
            foreach (var index in file.Chunks)
            {
                if (index < 0 || index >= Chunks.Count)
                {
                    throw new InvalidDataException($"Manifest file {file.Path} points to a missing chunk");
                }
                size += Chunks[index].Size;
            }
            if (size != file.Size)
            {
                throw new InvalidDataException($"Manifest file {file.Path}: chunks add up to {size}, not {file.Size}");
            }
            foreach (var patch in file.Patches ?? [])
            {
                if (!InPack(patch.Pack, patch.Offset, patch.Stored) || patch.From.Length != 32 || patch.To.Length != 32)
                {
                    throw new InvalidDataException($"Manifest patch for {file.Path} is out of range");
                }
            }
        }
    }

    /// <summary>
    /// Files the game or the launcher write themselves (settings, logs, crash reports in "Saved", version.txt):
    /// never part of a manifest, never deleted by an update.
    /// </summary>
    public static bool IsLocalState(string path) =>
        path.Split('/').Any(part => part.Equals("Saved", StringComparison.OrdinalIgnoreCase)) ||
        path.Equals("version.txt", StringComparison.OrdinalIgnoreCase);

    /// <summary>Relative, forward slashes, no "..", no drive or root: a manifest can never write outside the game folder.</summary>
    public static bool PathIsSafe(string path) =>
        path.Length > 0 && !path.StartsWith('/') && !path.Contains('\\') && !path.Contains(':') &&
        path.Split('/').All(part => part.Length > 0 && part != "." && part != "..");
}

public sealed class PatchPack
{
    /// <summary>Release whose asset it is.</summary>
    [JsonPropertyName("tag")] public string Tag { get; set; } = "";
    [JsonPropertyName("asset")] public string Asset { get; set; } = "";
    [JsonPropertyName("size")] public long Size { get; set; }
}

/// <summary>A chunk in a pack: Brotli when the stored size is smaller than the size, otherwise the raw bytes.</summary>
public sealed class PatchChunk
{
    [JsonPropertyName("h")] public string Hash { get; set; } = "";
    /// <summary>Bytes of the chunk itself.</summary>
    [JsonPropertyName("n")] public int Size { get; set; }
    [JsonPropertyName("p")] public int Pack { get; set; }
    [JsonPropertyName("o")] public long Offset { get; set; }
    /// <summary>Bytes in the pack.</summary>
    [JsonPropertyName("z")] public int Stored { get; set; }
}

public sealed class PatchFile
{
    /// <summary>Relative to the game folder, with forward slashes.</summary>
    [JsonPropertyName("path")] public string Path { get; set; } = "";
    [JsonPropertyName("size")] public long Size { get; set; }
    [JsonPropertyName("chunks")] public List<int> Chunks { get; set; } = [];
    /// <summary>Binary patches between versions of this file; the last one ends at this file's content.</summary>
    [JsonPropertyName("patches")] public List<PatchEdge>? Patches { get; set; }
}

/// <summary>One binary patch in a pack: turns the file with id From into the file with id To.</summary>
public sealed class PatchEdge
{
    [JsonPropertyName("from")] public string From { get; set; } = "";
    [JsonPropertyName("to")] public string To { get; set; } = "";
    [JsonPropertyName("p")] public int Pack { get; set; }
    [JsonPropertyName("o")] public long Offset { get; set; }
    [JsonPropertyName("z")] public long Stored { get; set; }
}
