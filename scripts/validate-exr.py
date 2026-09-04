#!/usr/bin/env python3
"""Validate an uncompressed scanline EXR: header sanity + pixel statistics."""
import struct
import sys


def half_to_float(h):
    s = (h >> 15) & 1
    e = (h >> 10) & 0x1f
    m = h & 0x3ff
    if e == 0:
        v = m / 1024.0 * 2.0 ** -14
    elif e == 31:
        v = float('inf') if m == 0 else float('nan')
    else:
        v = (1 + m / 1024.0) * 2.0 ** (e - 15)
    return -v if s else v


def read_exact(f, size):
    data = f.read(size)
    assert len(data) == size, "truncated header"
    return data


def read_cstr(f):
    s = b''
    while (c := read_exact(f, 1)) != b'\0':
        s += c
    return s


def read_attrs(f):
    attrs = {}
    while True:
        name = read_cstr(f)
        if name == b'':
            break
        atype = read_cstr(f)
        size = struct.unpack('<i', read_exact(f, 4))[0]
        attrs[name.decode()] = (atype.decode(), read_exact(f, size))
    return attrs


def main(path):
    f = open(path, 'rb')
    magic, version = struct.unpack('<ii', f.read(8))
    assert magic == 20000630, f"bad magic {magic:#x}"
    attrs = read_attrs(f)

    ch_raw = attrs['channels'][1]
    channels = []
    off = 0
    while ch_raw[off] != 0:
        end = ch_raw.index(b'\0', off)
        cname = ch_raw[off:end].decode()
        ptype = struct.unpack('<i', ch_raw[end + 1:end + 5])[0]
        channels.append((cname, ptype))  # 0=uint,1=half,2=float
        off = end + 1 + 16
    # readers walk the list to its terminator: any slack desyncs the header
    assert off + 1 == len(ch_raw), f"chlist declares {len(ch_raw)} bytes, entries end at {off + 1}"
    names = [c[0] for c in channels]
    assert names == sorted(names), f"channels not sorted: {names}"
    xmin, ymin, xmax, ymax = struct.unpack('<4i', attrs['dataWindow'][1])
    width, height = xmax - xmin + 1, ymax - ymin + 1
    comp = attrs['compression'][1][0]
    print(f"{path}: {width}x{height} channels={channels} compression={comp} "
          f"chromaticities={'chromaticities' in attrs}")
    assert comp == 0, "not uncompressed"

    # line offset table, then per-scanline: y:int32, size:int32, data
    f.read(8 * height)
    psz = {0: 4, 1: 2, 2: 4}
    stats = {c[0]: [float('inf'), float('-inf'), 0.0, 0] for c in channels}
    step = max(1, height // 16)
    line0 = f.tell()
    lsize = 8 + sum(psz[t] for _, t in channels) * width
    for y in range(0, height, step):
        f.seek(line0 + y * lsize)
        ly, lsz = struct.unpack('<ii', f.read(8))
        assert lsz == lsize - 8, f"scanline size {lsz} != {lsize - 8}"
        # channels stored alphabetically-as-written, planar per scanline
        for cname, ptype in channels:
            raw = f.read(psz[ptype] * width)
            for x in range(0, width, max(1, width // 64)):
                if ptype == 1:
                    v = half_to_float(struct.unpack_from('<H', raw, x * 2)[0])
                else:
                    v = struct.unpack_from('<f', raw, x * 4)[0]
                st = stats[cname]
                st[0] = min(st[0], v)
                st[1] = max(st[1], v)
                st[2] += v
                st[3] += 1
    ok = False
    for cname, st in stats.items():
        mean = st[2] / max(st[3], 1)
        print(f"  {cname}: min={st[0]:.4f} max={st[1]:.4f} mean={mean:.4f} n={st[3]}")
        if cname != 'A' and st[1] > st[0] and st[1] > 0.0:
            ok = True
    print(f"  content: {'VARIED (looks like a real frame)' if ok else 'FLAT/EMPTY'}")
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
