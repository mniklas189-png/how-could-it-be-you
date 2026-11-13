using System.Buffers;
using System.Buffers.Binary;
using System.Security.Cryptography;

namespace HowCouldItBeYou.Launcher.Patching;

/// <summary>128 bits of a chunk's SHA-256: names a chunk in the manifest and in the local index.</summary>
public readonly record struct ChunkHash(ulong Hi, ulong Lo)
{
    public static ChunkHash Of(ReadOnlySpan<byte> data)
    {
        Span<byte> sha = stackalloc byte[32];
        SHA256.HashData(data, sha);
        return new(BinaryPrimitives.ReadUInt64BigEndian(sha), BinaryPrimitives.ReadUInt64BigEndian(sha[8..]));
    }

    public static ChunkHash Parse(string hex)
    {
        if (hex.Length != 32)
        {
            throw new FormatException($"Chunk hash must have 32 hex digits: {hex}");
        }
        return new(Convert.ToUInt64(hex[..16], 16), Convert.ToUInt64(hex[16..], 16));
    }

    public override string ToString() => $"{Hi:x16}{Lo:x16}";
}

/// <summary>A piece of a file: where it lies and what it holds.</summary>
public readonly record struct FileChunk(long Offset, int Length, ChunkHash Hash);

/// <summary>
/// Splits files into content-defined chunks (FastCDC with a gear hash). A cut depends only on the bytes right
/// before it, so when a build inserts or removes data, the chunks before and after the change stay the same and
/// only the few chunks around the change are new. The release tool (Tools/PatchTool) and the launcher must cut
/// in exactly the same way: never change the table, the masks or the sizes without a new manifest format.
/// </summary>
public static class ContentChunker
{
    public const int MinSize = 16 * 1024;
    public const int AvgSize = 64 * 1024;
    public const int MaxSize = 256 * 1024;

    /// <summary>Name in the manifest, so a launcher never mixes chunks of a different cutting rule.</summary>
    public const string Algorithm = "fastcdc-gear64-16k-64k-256k";

    // Normalized chunking: harder to cut before the average size (18 bits), easier after it (14 bits).
    // High bits, because bit k of the gear hash depends on the last k+1 bytes only.
    private const ulong MaskSmall = ((1UL << 18) - 1) << (64 - 18);
    private const ulong MaskLarge = ((1UL << 14) - 1) << (64 - 14);

    private static readonly ulong[] Gear = BuildGear();

    private static ulong[] BuildGear()
    {
        // SplitMix64 from a fixed seed: the same table on every machine and in every version
        var table = new ulong[256];
        var state = 0x4843494259554344UL; // "HCIBYUCD"
        for (var i = 0; i < table.Length; i++)
        {
            state += 0x9E3779B97F4A7C15UL;
            var z = state;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9UL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBUL;
            table[i] = z ^ (z >> 31);
        }
        return table;
    }

    /// <summary>Length of the next chunk at the start of <paramref name="data"/> (data is at most MaxSize long, or the rest of the file).</summary>
    public static int NextCut(ReadOnlySpan<byte> data)
    {
        var n = Math.Min(data.Length, MaxSize);
        if (n <= MinSize)
        {
            return n;
        }
        var gear = Gear;
        ulong h = 0;
        var i = MinSize;
        var normal = Math.Min(n, AvgSize);
        for (; i < normal; i++)
        {
            h = (h << 1) + gear[data[i]];
            if ((h & MaskSmall) == 0)
            {
                return i + 1;
            }
        }
        for (; i < n; i++)
        {
            h = (h << 1) + gear[data[i]];
            if ((h & MaskLarge) == 0)
            {
                return i + 1;
            }
        }
        return n;
    }

    /// <summary>
    /// Cuts a whole stream into chunks. <paramref name="onChunk"/> gets each chunk's position, hash and bytes
    /// (the span is only valid during the call). <paramref name="onRead"/> reports bytes read, for progress.
    /// </summary>
    public static void Split(Stream stream, Action<FileChunk, ReadOnlySpan<byte>>? onChunk, Action<FileChunk>? onHash = null,
        Action<long>? onRead = null, CancellationToken ct = default)
    {
        const int bufferSize = 8 * 1024 * 1024;
        var buffer = ArrayPool<byte>.Shared.Rent(bufferSize);
        try
        {
            var filled = 0;
            var start = 0;
            long offset = 0;
            var eof = false;
            while (true)
            {
                if (!eof && filled - start < MaxSize)
                {
                    // Move the rest to the front and fill up
                    if (start > 0)
                    {
                        Buffer.BlockCopy(buffer, start, buffer, 0, filled - start);
                        filled -= start;
                        start = 0;
                    }
                    while (filled < bufferSize)
                    {
                        var read = stream.Read(buffer, filled, bufferSize - filled);
                        if (read == 0)
                        {
                            eof = true;
                            break;
                        }
                        filled += read;
                        onRead?.Invoke(read);
                    }
                    ct.ThrowIfCancellationRequested();
                }
                if (filled == start)
                {
                    return;
                }
                var span = new ReadOnlySpan<byte>(buffer, start, filled - start);
                var cut = NextCut(span);
                var bytes = span[..cut];
                var chunk = new FileChunk(offset, cut, ChunkHash.Of(bytes));
                onChunk?.Invoke(chunk, bytes);
                onHash?.Invoke(chunk);
                offset += cut;
                start += cut;
            }
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(buffer);
        }
    }

    /// <summary>All chunks of a file, in order.</summary>
    public static List<FileChunk> SplitFile(string path, Action<long>? onRead = null, CancellationToken ct = default)
    {
        var chunks = new List<FileChunk>();
        using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1, FileOptions.SequentialScan);
        Split(stream, null, chunks.Add, onRead, ct);
        return chunks;
    }
}
