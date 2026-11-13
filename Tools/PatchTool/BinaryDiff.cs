using System.Buffers.Binary;
using System.IO.Compression;
using HowCouldItBeYou.Launcher.Patching;

namespace HowCouldItBeYou.PatchTool;

/// <summary>
/// Creates a binary patch old → new in the format of Launcher/Patching/BinaryPatch.cs (bsdiff 4 method by
/// Colin Percival: approximate matches found through a suffix array of the old file; the byte-wise difference
/// of a match is mostly zeros because only shifted addresses changed, and Brotli packs that to almost nothing).
/// Needs about 10 bytes of memory per old byte plus both files; a 380 MB .pdb takes a few minutes.
/// </summary>
internal static class BinaryDiff
{
    public static byte[] Create(byte[] oldData, byte[] newData, int quality, Action<string>? log = null)
    {
        var watch = System.Diagnostics.Stopwatch.StartNew();
        var suffixes = SuffixArray.Build(oldData);
        var prefixStart = PrefixIndex(suffixes, oldData);
        var sortTime = watch.Elapsed.TotalSeconds;

        // The new file is scanned in independent segments on all cores. Each segment starts with the old
        // position at 0, so a (0, 0, -position) triple between segments moves it back there.
        var segmentCount = Math.Clamp(newData.Length / (4 << 20), 1, Environment.ProcessorCount * 2);
        var segments = new Segment[segmentCount];
        Parallel.For(0, segmentCount, k =>
        {
            var start = (int)((long)newData.Length * k / segmentCount);
            var end = (int)((long)newData.Length * (k + 1) / segmentCount);
            segments[k] = Scan(suffixes, prefixStart, oldData, newData.AsSpan(start, end - start));
        });
        var ctrl = new MemoryStream();
        var diff = new MemoryStream();
        var extra = new MemoryStream();
        var triple = new byte[24];
        for (var k = 0; k < segmentCount; k++)
        {
            if (k > 0)
            {
                BinaryPrimitives.WriteInt64LittleEndian(triple, 0);
                BinaryPrimitives.WriteInt64LittleEndian(triple.AsSpan(8), 0);
                BinaryPrimitives.WriteInt64LittleEndian(triple.AsSpan(16), -segments[k - 1].FinalOldPos);
                ctrl.Write(triple);
            }
            segments[k].Ctrl.WriteTo(ctrl);
            segments[k].Diff.WriteTo(diff);
            segments[k].Extra.WriteTo(extra);
        }
        var scanTime = watch.Elapsed.TotalSeconds - sortTime;

        // Container
        var output = new MemoryStream();
        output.Write(BinaryPatch.Magic);
        output.Write(new byte[BinaryPatch.HeaderSize - 8]); // filled in below
        var ctrlStart = output.Position;
        WriteSection(output, ctrl, quality);
        var diffStart = output.Position;
        WriteSection(output, diff, quality);
        var extraStart = output.Position;
        WriteSection(output, extra, quality);
        var end = output.Position;

        log?.Invoke($"sort {sortTime:0} s, scan {scanTime:0} s ({segmentCount} parts), pack {watch.Elapsed.TotalSeconds - sortTime - scanTime:0} s");
        var result = output.ToArray();
        var h = result.AsSpan(8);
        BinaryPrimitives.WriteInt64LittleEndian(h, oldData.Length);
        BinaryPrimitives.WriteInt64LittleEndian(h[8..], newData.Length);
        BinaryPrimitives.WriteInt64LittleEndian(h[16..], ctrlStart);
        BinaryPrimitives.WriteInt64LittleEndian(h[24..], diffStart);
        BinaryPrimitives.WriteInt64LittleEndian(h[32..], extraStart);
        BinaryPrimitives.WriteInt64LittleEndian(h[40..], end);
        return result;
    }

    private sealed record Segment(MemoryStream Ctrl, MemoryStream Diff, MemoryStream Extra, long FinalOldPos);

    /// <summary>bsdiff's main loop over one part of the new file (positions relative to that part).</summary>
    private static Segment Scan(int[] suffixes, int[] prefixStart, byte[] oldData, ReadOnlySpan<byte> newData)
    {
        var ctrl = new MemoryStream();
        var diff = new MemoryStream();
        var extra = new MemoryStream();
        Span<byte> triple = stackalloc byte[24];

        int oldSize = oldData.Length, newSize = newData.Length;
        int scan = 0, len = 0, pos = 0, lastScan = 0, lastPos = 0, lastOffset = 0;
        var buffer = new byte[1 << 16];
        while (scan < newSize)
        {
            var oldScore = 0;
            int scsc;
            for (scsc = scan += len; scan < newSize; scan++)
            {
                len = Search(suffixes, prefixStart, oldData, newData[scan..], out pos);
                for (; scsc < scan + len; scsc++)
                {
                    if (scsc + lastOffset < oldSize && oldData[scsc + lastOffset] == newData[scsc])
                    {
                        oldScore++;
                    }
                }
                if ((len == oldScore && len != 0) || len > oldScore + 8)
                {
                    break;
                }
                if (scan + lastOffset < oldSize && oldData[scan + lastOffset] == newData[scan])
                {
                    oldScore--;
                }
            }

            if (len != oldScore || scan == newSize)
            {
                // Extend the previous match forwards and this match backwards as long as most bytes agree
                int s = 0, sf = 0, lenF = 0;
                for (var i = 0; lastScan + i < scan && lastPos + i < oldSize;)
                {
                    if (oldData[lastPos + i] == newData[lastScan + i])
                    {
                        s++;
                    }
                    i++;
                    if (s * 2 - i > sf * 2 - lenF)
                    {
                        sf = s;
                        lenF = i;
                    }
                }

                var lenB = 0;
                if (scan < newSize)
                {
                    s = 0;
                    var sb = 0;
                    for (var i = 1; scan >= lastScan + i && pos >= i; i++)
                    {
                        if (oldData[pos - i] == newData[scan - i])
                        {
                            s++;
                        }
                        if (s * 2 - i > sb * 2 - lenB)
                        {
                            sb = s;
                            lenB = i;
                        }
                    }
                }

                if (lastScan + lenF > scan - lenB)
                {
                    var overlap = lastScan + lenF - (scan - lenB);
                    s = 0;
                    int ss = 0, lenS = 0;
                    for (var i = 0; i < overlap; i++)
                    {
                        if (newData[lastScan + lenF - overlap + i] == oldData[lastPos + lenF - overlap + i])
                        {
                            s++;
                        }
                        if (newData[scan - lenB + i] == oldData[pos - lenB + i])
                        {
                            s--;
                        }
                        if (s > ss)
                        {
                            ss = s;
                            lenS = i + 1;
                        }
                    }
                    lenF += lenS - overlap;
                    lenB -= lenS;
                }

                for (var done = 0; done < lenF;)
                {
                    var n = Math.Min(buffer.Length, lenF - done);
                    for (var i = 0; i < n; i++)
                    {
                        buffer[i] = (byte)(newData[lastScan + done + i] - oldData[lastPos + done + i]);
                    }
                    diff.Write(buffer, 0, n);
                    done += n;
                }
                var extraLength = scan - lenB - (lastScan + lenF);
                extra.Write(newData.Slice(lastScan + lenF, extraLength));
                BinaryPrimitives.WriteInt64LittleEndian(triple, lenF);
                BinaryPrimitives.WriteInt64LittleEndian(triple[8..], extraLength);
                BinaryPrimitives.WriteInt64LittleEndian(triple[16..], pos - lenB - (lastPos + lenF));
                ctrl.Write(triple);

                lastScan = scan - lenB;
                lastPos = pos - lenB;
                lastOffset = pos - scan;
            }
        }
        return new Segment(ctrl, diff, extra, lastPos);
    }

    /// <summary>
    /// prefixStart[p] = first suffix-array index whose suffix starts with the two bytes p (big endian);
    /// prefixStart[65536] = number of suffixes. Suffixes shorter than two bytes sort before their group.
    /// </summary>
    private static int[] PrefixIndex(int[] suffixes, byte[] oldData)
    {
        var start = new int[65537];
        var p = 0;
        for (var i = 0; i < suffixes.Length; i++)
        {
            var at = suffixes[i];
            if (at + 2 > oldData.Length)
            {
                continue;
            }
            var key = (oldData[at] << 8) | oldData[at + 1];
            while (p <= key)
            {
                start[p++] = i;
            }
        }
        while (p <= 65536)
        {
            start[p++] = suffixes.Length;
        }
        return start;
    }

    private static void WriteSection(Stream output, MemoryStream section, int quality)
    {
        var raw = section.GetBuffer().AsMemory(0, (int)section.Length);
        var number = new byte[8];
        BinaryPrimitives.WriteInt64LittleEndian(number, raw.Length);
        output.Write(number);
        var blockCount = (raw.Length + BinaryPatch.BlockSize - 1) / BinaryPatch.BlockSize;
        var blocks = new byte[blockCount][];
        Parallel.For(0, blockCount, i =>
        {
            var block = raw.Span.Slice(i * BinaryPatch.BlockSize, Math.Min(BinaryPatch.BlockSize, raw.Length - i * BinaryPatch.BlockSize));
            var packed = new byte[BrotliEncoder.GetMaxCompressedLength(block.Length)];
            blocks[i] = BrotliEncoder.TryCompress(block, packed, out var written, quality, 24) && written < block.Length
                ? packed.AsSpan(0, written).ToArray()
                : block.ToArray();
        });
        for (var i = 0; i < blockCount; i++)
        {
            var rawLength = Math.Min(BinaryPatch.BlockSize, raw.Length - i * BinaryPatch.BlockSize);
            BinaryPrimitives.WriteInt32LittleEndian(number, rawLength);
            BinaryPrimitives.WriteInt32LittleEndian(number.AsSpan(4), blocks[i].Length);
            output.Write(number);
            output.Write(blocks[i]);
        }
    }

    /// <summary>Longest match of <paramref name="target"/> in the old data, by binary search over the suffix array.</summary>
    private static int Search(int[] suffixes, int[] prefixStart, byte[] oldData, ReadOnlySpan<byte> target, out int pos)
    {
        int st = 0, en = oldData.Length;
        if (target.Length >= 2)
        {
            // Every suffix before the group of the target's first two bytes is smaller, every one after it larger
            var key = (target[0] << 8) | target[1];
            st = Math.Max(prefixStart[key] - 1, 0);
            en = Math.Min(prefixStart[key + 1], oldData.Length);
            if (en <= st)
            {
                en = Math.Min(st + 1, oldData.Length);
                st = Math.Max(en - 1, 0);
            }
        }
        while (en - st >= 2)
        {
            var x = st + (en - st) / 2;
            var candidate = oldData.AsSpan(suffixes[x]);
            var length = Math.Min(candidate.Length, target.Length);
            if (candidate[..length].SequenceCompareTo(target[..length]) < 0)
            {
                st = x;
            }
            else
            {
                en = x;
            }
        }
        var a = oldData.AsSpan(suffixes[st]).CommonPrefixLength(target);
        var b = oldData.AsSpan(suffixes[en]).CommonPrefixLength(target);
        if (a > b)
        {
            pos = suffixes[st];
            return a;
        }
        pos = suffixes[en];
        return b;
    }
}

/// <summary>
/// Suffix array by induced sorting (SA-IS, Nong/Zhang/Chan 2009), linear time. Result has data.Length + 1
/// entries; entry 0 is the empty suffix (= data.Length), as bsdiff expects.
/// </summary>
internal static class SuffixArray
{
    public static int[] Build(byte[] data)
    {
        if (data.Length == 0)
        {
            return [0];
        }
        var n = data.Length + 1;
        var text = new int[n];
        for (var i = 0; i < data.Length; i++)
        {
            text[i] = data[i] + 1;
        }
        text[n - 1] = 0; // unique sentinel, smaller than every byte
        var sa = new int[n];
        Sais(text, sa, n, 256);
        return sa;
    }

    private static void GetBuckets(ReadOnlySpan<int> s, Span<int> bucket, int n, int k, bool end)
    {
        bucket[..(k + 1)].Clear();
        for (var i = 0; i < n; i++)
        {
            bucket[s[i]]++;
        }
        var sum = 0;
        for (var i = 0; i <= k; i++)
        {
            sum += bucket[i];
            bucket[i] = end ? sum : sum - bucket[i];
        }
    }

    private static void InduceL(bool[] t, Span<int> sa, ReadOnlySpan<int> s, Span<int> bucket, int n, int k)
    {
        GetBuckets(s, bucket, n, k, end: false);
        for (var i = 0; i < n; i++)
        {
            var j = sa[i] - 1;
            if (j >= 0 && !t[j])
            {
                sa[bucket[s[j]]++] = j;
            }
        }
    }

    private static void InduceS(bool[] t, Span<int> sa, ReadOnlySpan<int> s, Span<int> bucket, int n, int k)
    {
        GetBuckets(s, bucket, n, k, end: true);
        for (var i = n - 1; i >= 0; i--)
        {
            var j = sa[i] - 1;
            if (j >= 0 && t[j])
            {
                sa[--bucket[s[j]]] = j;
            }
        }
    }

    /// <summary>s[0..n-1] over {0..k}, s[n-1] = 0 unique. t[i] = true for S-type positions.</summary>
    private static void Sais(Span<int> s, Span<int> sa, int n, int k)
    {
        var t = new bool[n];
        t[n - 1] = true;
        if (n >= 2)
        {
            t[n - 2] = false;
        }
        for (var i = n - 3; i >= 0; i--)
        {
            t[i] = s[i] < s[i + 1] || (s[i] == s[i + 1] && t[i + 1]);
        }
        bool IsLms(int i) => i > 0 && t[i] && !t[i - 1];

        // Stage 1: sort the LMS substrings
        var bucket = new int[k + 1];
        GetBuckets(s, bucket, n, k, end: true);
        sa[..n].Fill(-1);
        for (var i = 1; i < n; i++)
        {
            if (IsLms(i))
            {
                sa[--bucket[s[i]]] = i;
            }
        }
        InduceL(t, sa, s, bucket, n, k);
        InduceS(t, sa, s, bucket, n, k);

        var n1 = 0;
        for (var i = 0; i < n; i++)
        {
            if (IsLms(sa[i]))
            {
                sa[n1++] = sa[i];
            }
        }

        // Name the LMS substrings
        sa[n1..n].Fill(-1);
        int name = 0, prev = -1;
        for (var i = 0; i < n1; i++)
        {
            var pos = sa[i];
            var diff = false;
            for (var d = 0; d < n; d++)
            {
                if (prev == -1 || s[pos + d] != s[prev + d] || t[pos + d] != t[prev + d])
                {
                    diff = true;
                    break;
                }
                if (d > 0 && (IsLms(pos + d) || IsLms(prev + d)))
                {
                    break;
                }
            }
            if (diff)
            {
                name++;
                prev = pos;
            }
            sa[n1 + pos / 2] = name - 1;
        }
        for (int i = n - 1, j = n - 1; i >= n1; i--)
        {
            if (sa[i] >= 0)
            {
                sa[j--] = sa[i];
            }
        }

        // Stage 2: solve the reduced problem
        var sa1 = sa[..n1];
        var s1 = sa.Slice(n - n1, n1);
        if (name < n1)
        {
            Sais(s1, sa1, n1, name - 1);
        }
        else
        {
            for (var i = 0; i < n1; i++)
            {
                sa1[s1[i]] = i;
            }
        }

        // Stage 3: induce the full suffix array from the sorted LMS suffixes
        GetBuckets(s, bucket, n, k, end: true);
        for (int i = 1, j = 0; i < n; i++)
        {
            if (IsLms(i))
            {
                s1[j++] = i;
            }
        }
        for (var i = 0; i < n1; i++)
        {
            sa1[i] = s1[sa1[i]];
        }
        sa[n1..n].Fill(-1);
        for (var i = n1 - 1; i >= 0; i--)
        {
            var j = sa[i];
            sa[i] = -1;
            sa[--bucket[s[j]]] = j;
        }
        InduceL(t, sa, s, bucket, n, k);
        InduceS(t, sa, s, bucket, n, k);
    }
}
