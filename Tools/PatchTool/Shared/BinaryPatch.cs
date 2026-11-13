using System.Buffers.Binary;
using System.IO.Compression;
using System.Numerics;

namespace HowCouldItBeYou.Launcher.Patching;

/// <summary>
/// Applies a binary patch (bsdiff method, own container "HCBDIF01", written by Tools/PatchTool/BinaryDiff.cs).
/// Used for the game's .exe/.pdb (Linux: the program and its .debug/.sym): a code change shifts addresses all
/// over these files, so nearly every chunk differs, while a byte-wise difference is mostly zeros and packs tiny.
///
/// Layout: magic, old size, new size, start of the control/diff/extra sections and the end (7 x 8 bytes).
/// Each section: total raw length (8 bytes), then blocks of [raw length (4)][stored length (4)][bytes],
/// a block is Brotli when stored &lt; raw. Control = triples (add, copy, seek) of signed 64-bit numbers:
/// add bytes = old bytes + diff bytes, then copy bytes from extra, then move the old position by seek.
/// </summary>
public static class BinaryPatch
{
    public static ReadOnlySpan<byte> Magic => "HCBDIF01"u8;
    public const int HeaderSize = 7 * 8;
    public const int BlockSize = 4 << 20;

    public static (long OldSize, long NewSize) ReadSizes(ReadOnlySpan<byte> patch)
    {
        if (patch.Length < HeaderSize || !patch[..8].SequenceEqual(Magic))
        {
            throw new InvalidDataException("Not a binary patch");
        }
        return (BinaryPrimitives.ReadInt64LittleEndian(patch[8..]), BinaryPrimitives.ReadInt64LittleEndian(patch[16..]));
    }

    /// <summary>Writes the new file to <paramref name="output"/>. <paramref name="oldFile"/> must be seekable.</summary>
    public static void Apply(byte[] patch, Stream oldFile, Stream output, Action<long>? onWritten = null, CancellationToken ct = default)
    {
        var (oldSize, newSize) = ReadSizes(patch);
        if (oldFile.Length != oldSize)
        {
            throw new InvalidDataException($"Patch expects an old file of {oldSize} bytes, found {oldFile.Length}");
        }
        long Offset(int index)
        {
            var value = BinaryPrimitives.ReadInt64LittleEndian(patch.AsSpan(24 + index * 8));
            return value >= HeaderSize && value <= patch.Length ? value : throw new InvalidDataException("Broken patch header");
        }
        var ctrlReader = new SectionReader(patch, Offset(0), Offset(1));
        var diff = new SectionReader(patch, Offset(1), Offset(2));
        var extra = new SectionReader(patch, Offset(2), Offset(3));

        var ctrl = new byte[ctrlReader.RawLength];
        ctrlReader.ReadExactly(ctrl);
        if (ctrl.Length % 24 != 0)
        {
            throw new InvalidDataException("Broken patch control data");
        }

        var diffBuffer = new byte[1 << 20];
        var oldBuffer = new byte[1 << 20];
        long oldPos = 0, newPos = 0;
        for (var c = 0; c < ctrl.Length; c += 24)
        {
            ct.ThrowIfCancellationRequested();
            var add = BinaryPrimitives.ReadInt64LittleEndian(ctrl.AsSpan(c));
            var copy = BinaryPrimitives.ReadInt64LittleEndian(ctrl.AsSpan(c + 8));
            var seek = BinaryPrimitives.ReadInt64LittleEndian(ctrl.AsSpan(c + 16));
            if (add < 0 || copy < 0 || newPos + add + copy > newSize)
            {
                throw new InvalidDataException("Broken patch control triple");
            }

            for (var left = add; left > 0;)
            {
                var n = (int)Math.Min(left, diffBuffer.Length);
                var d = diffBuffer.AsSpan(0, n);
                diff.ReadExactly(d);
                var o = oldBuffer.AsSpan(0, n);
                ReadOld(oldFile, oldSize, oldPos, o);
                AddBytes(d, o);
                output.Write(d);
                oldPos += n;
                newPos += n;
                left -= n;
                onWritten?.Invoke(n);
            }
            for (var left = copy; left > 0;)
            {
                var n = (int)Math.Min(left, diffBuffer.Length);
                var e = diffBuffer.AsSpan(0, n);
                extra.ReadExactly(e);
                output.Write(e);
                newPos += n;
                left -= n;
                onWritten?.Invoke(n);
            }
            oldPos += seek;
        }
        if (newPos != newSize)
        {
            throw new InvalidDataException($"Patch produced {newPos} bytes instead of {newSize}");
        }
    }

    /// <summary>Old bytes at a position; outside the old file they count as zero (as in bspatch).</summary>
    private static void ReadOld(Stream oldFile, long oldSize, long position, Span<byte> target)
    {
        target.Clear();
        var start = Math.Max(position, 0);
        var end = Math.Min(position + target.Length, oldSize);
        if (end <= start)
        {
            return;
        }
        var slice = target.Slice((int)(start - position), (int)(end - start));
        if (oldFile.Position != start)
        {
            oldFile.Position = start;
        }
        oldFile.ReadExactly(slice);
    }

    /// <summary>target[i] += source[i] (wrapping bytes).</summary>
    private static void AddBytes(Span<byte> target, ReadOnlySpan<byte> source)
    {
        var i = 0;
        if (Vector.IsHardwareAccelerated)
        {
            for (; i <= target.Length - Vector<byte>.Count; i += Vector<byte>.Count)
            {
                (new Vector<byte>(target[i..]) + new Vector<byte>(source[i..])).CopyTo(target[i..]);
            }
        }
        for (; i < target.Length; i++)
        {
            target[i] += source[i];
        }
    }

    /// <summary>Reads one section block by block.</summary>
    private sealed class SectionReader
    {
        private readonly byte[] _patch;
        private readonly long _end;
        private long _next;
        private byte[] _block = [];
        private int _blockLength;
        private int _blockPos;
        public long RawLength { get; }
        private long _delivered;

        public SectionReader(byte[] patch, long start, long end)
        {
            if (end < start + 8)
            {
                throw new InvalidDataException("Broken patch section");
            }
            _patch = patch;
            _end = end;
            RawLength = BinaryPrimitives.ReadInt64LittleEndian(patch.AsSpan((int)start));
            _next = start + 8;
        }

        public void ReadExactly(Span<byte> target)
        {
            if (_delivered + target.Length > RawLength)
            {
                throw new InvalidDataException("Patch section is shorter than its control data says");
            }
            while (target.Length > 0)
            {
                if (_blockPos == _blockLength)
                {
                    NextBlock();
                }
                var n = Math.Min(target.Length, _blockLength - _blockPos);
                _block.AsSpan(_blockPos, n).CopyTo(target);
                _blockPos += n;
                _delivered += n;
                target = target[n..];
            }
        }

        private void NextBlock()
        {
            if (_next + 8 > _end)
            {
                throw new InvalidDataException("Patch section ended early");
            }
            var raw = BinaryPrimitives.ReadInt32LittleEndian(_patch.AsSpan((int)_next));
            var stored = BinaryPrimitives.ReadInt32LittleEndian(_patch.AsSpan((int)_next + 4));
            _next += 8;
            if (raw <= 0 || raw > BlockSize || stored <= 0 || stored > raw || _next + stored > _end)
            {
                throw new InvalidDataException("Broken patch block");
            }
            if (_block.Length < raw)
            {
                _block = new byte[BlockSize];
            }
            var source = _patch.AsSpan((int)_next, stored);
            if (stored == raw)
            {
                source.CopyTo(_block);
            }
            else if (!BrotliDecoder.TryDecompress(source, _block.AsSpan(0, raw), out var written) || written != raw)
            {
                throw new InvalidDataException("Patch block does not unpack");
            }
            _next += stored;
            _blockLength = raw;
            _blockPos = 0;
        }
    }
}
