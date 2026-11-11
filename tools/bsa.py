#!/usr/bin/env python3
"""Skyrim SE archives (BSA version 105) without extra modules: read vanilla assets, write the mod's own.

    python3 tools/bsa.py list ARCHIVE [PATTERN]          names matching a regular expression
    python3 tools/bsa.py bounds ARCHIVE NAME [NAME...]   size and axes of each mesh's shapes

write() packs files into a new archive (used by build.py); the commands only read.
"""

import os
import re
import struct
import sys

COMPRESSED = 0x4
EMBEDDED_NAMES = 0x100
SIZE_COMPRESSION_TOGGLE = 0x40000000
VERSION = 105
INCLUDE_FOLDER_NAMES = 0x1
INCLUDE_FILE_NAMES = 0x2
RETAIN_FILE_NAMES = 0x10  # set on the vanilla sound archive
# Content flags for the header: what kinds of file the archive holds.
CONTENT = {'.nif': 0x1, '.dds': 0x2, '.xml': 0x4, '.swf': 0x4, '.wav': 0x8, '.xwm': 0x8, '.fuz': 0x10}


def name_hash(name, folder=False):
    """The game's hash of a lowercase path or file name, which it looks files up by."""
    name = name.lower().replace('/', '\\')
    root, ext = (name, '') if folder else os.path.splitext(name)
    raw = root.encode('latin1')
    length = len(raw)
    low = 0
    if length:
        low = raw[-1] | ((raw[-2] if length > 2 else 0) << 8) | (length << 16) | (raw[0] << 24)
    low |= {'.kf': 0x80, '.nif': 0x8000, '.dds': 0x8080, '.wav': 0x80000000}.get(ext, 0)
    middle = 0
    for c in raw[1:-2] if length > 3 else b'':
        middle = (middle * 0x1003F + c) & 0xFFFFFFFF
    tail = 0
    for c in ext.encode('latin1'):
        tail = (tail * 0x1003F + c) & 0xFFFFFFFF
    return (((middle + tail) & 0xFFFFFFFF) << 32) | low


def write(path, files):
    """Writes an uncompressed archive (sounds must not be compressed) of {path inside the archive: bytes}."""
    folders = {}
    for name, data in files.items():
        folder, _, file = name.lower().replace('/', '\\').rpartition('\\')
        folders.setdefault(folder, {})[file] = data
    order = sorted(folders, key=lambda f: name_hash(f, folder=True))
    contents = {f: sorted(folders[f], key=name_hash) for f in order}
    file_count = sum(len(names) for names in contents.values())
    folder_names_length = sum(len(f) + 1 for f in order)
    file_names = b''.join(n.encode('latin1') + b'\0' for f in order for n in contents[f])
    content_flags = 0
    for f in order:
        for n in contents[f]:
            content_flags |= CONTENT.get(os.path.splitext(n)[1], 0)

    header_size, folder_record_size, file_record_size = 36, 24, 16
    records_start = header_size + folder_record_size * len(order)
    blocks_size = sum(1 + len(f) + 1 + file_record_size * len(contents[f]) for f in order)
    data_offset = records_start + blocks_size + len(file_names)

    out = bytearray(struct.pack('<4sIIIIIIIHH', b'BSA\0', VERSION, header_size,
                                INCLUDE_FOLDER_NAMES | INCLUDE_FILE_NAMES | RETAIN_FILE_NAMES, len(order), file_count,
                                folder_names_length, len(file_names), content_flags, 0))
    block_at = records_start
    for f in order:
        # The offset counts the file names' length as well, as the game expects.
        out += struct.pack('<QIIQ', name_hash(f, folder=True), len(contents[f]), 0, block_at + len(file_names))
        block_at += 1 + len(f) + 1 + file_record_size * len(contents[f])
    at = data_offset
    for f in order:
        out += bytes([len(f) + 1]) + f.encode('latin1') + b'\0'
        for n in contents[f]:
            size = len(folders[f][n])
            out += struct.pack('<QII', name_hash(n), size, at)
            at += size
    out += file_names
    for f in order:
        for n in contents[f]:
            out += folders[f][n]
    with open(path, 'wb') as handle:
        handle.write(out)


def lz4_block(data, out, end_size=None):
    i = 0
    while i < len(data):
        token = data[i]
        i += 1
        length = token >> 4
        if length == 15:
            while True:
                extra = data[i]
                i += 1
                length += extra
                if extra != 255:
                    break
        out += data[i:i + length]
        i += length
        if i >= len(data):
            break
        offset = data[i] | (data[i + 1] << 8)
        i += 2
        match = token & 15
        if match == 15:
            while True:
                extra = data[i]
                i += 1
                match += extra
                if extra != 255:
                    break
        match += 4
        start = len(out) - offset
        for k in range(match):
            out.append(out[start + k])
    return out


def lz4_frame(data):
    if struct.unpack_from("<I", data)[0] != 0x184D2204:
        raise ValueError("not an LZ4 frame")
    flags = data[4]
    i = 6 + (8 if flags & 0x08 else 0) + (4 if flags & 0x01 else 0) + 1
    block_checksum = bool(flags & 0x10)
    out = bytearray()
    while True:
        size = struct.unpack_from("<I", data, i)[0]
        i += 4
        if size == 0:
            break
        raw = size & 0x80000000
        size &= 0x7FFFFFFF
        block = data[i:i + size]
        i += size + (4 if block_checksum else 0)
        if raw:
            out += block
        else:
            lz4_block(block, out)
    return bytes(out)


class Archive:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            header = f.read(36)
            magic, version, _, self.flags, folders, files, _, names_length, _, _ = struct.unpack(
                "<4sIIIIIIIHH", header)
            if magic != b"BSA\0" or version != 105:
                raise ValueError(f"{path}: not a Skyrim SE archive")
            records = [struct.unpack("<QIIQ", f.read(24)) for _ in range(folders)]
            entries = []
            for _, count, _, _ in records:
                length = f.read(1)[0]
                folder = f.read(length)[:-1].decode("latin1")
                for _ in range(count):
                    _, size, offset = struct.unpack("<QII", f.read(16))
                    entries.append((folder, size, offset))
            names = f.read(names_length).split(b"\0")
        self.files = {}
        for (folder, size, offset), name in zip(entries, names):
            self.files[f"{folder}\\{name.decode('latin1')}".lower()] = (size, offset)

    def read(self, name):
        size, offset = self.files[name.lower()]
        compressed = bool(self.flags & COMPRESSED) != bool(size & SIZE_COMPRESSION_TOGGLE)
        size &= ~SIZE_COMPRESSION_TOGGLE
        with open(self.path, "rb") as f:
            f.seek(offset)
            data = f.read(size)
        if self.flags & EMBEDDED_NAMES:
            data = data[1 + data[0]:]
        if compressed:
            data = lz4_frame(data[4:])
        return data


def nif_shapes(data):
    """Yields (block type, positions) for each BSTriShape in a Skyrim SE NIF."""
    i = data.index(b"\n") + 1
    version, endian, user, blocks = struct.unpack_from("<IBII", data, i)
    i += 13
    bs_version = struct.unpack_from("<I", data, i)[0]
    i += 4
    for _ in range(3):  # author, process script, export script
        length = data[i]
        i += 1 + length
    type_count = struct.unpack_from("<H", data, i)[0]
    i += 2
    types = []
    for _ in range(type_count):
        length = struct.unpack_from("<I", data, i)[0]
        types.append(data[i + 4:i + 4 + length].decode())
        i += 4 + length
    block_types = struct.unpack_from(f"<{blocks}H", data, i)
    i += 2 * blocks
    sizes = struct.unpack_from(f"<{blocks}I", data, i)
    i += 4 * blocks
    string_count, _ = struct.unpack_from("<II", data, i)
    i += 8
    for _ in range(string_count):
        length = struct.unpack_from("<I", data, i)[0]
        i += 4 + length
    group_count = struct.unpack_from("<I", data, i)[0]
    i += 4 + 4 * group_count
    for index in range(blocks):
        kind = types[block_types[index] & 0x7FFF]
        start = i
        i += sizes[index]
        if kind not in ("BSTriShape", "BSDynamicTriShape", "BSMeshLODTriShape", "BSLODTriShape"):
            continue
        j = start + 4  # name
        extra = struct.unpack_from("<I", data, j)[0]
        j += 4 + 4 * extra + 4  # extra data, controller
        j += 4  # flags
        translation = struct.unpack_from("<3f", data, j)
        j += 12 + 36 + 4 + 4  # rotation, scale, collision
        j += 16  # bounding sphere
        j += 12  # skin, shader, alpha
        desc = struct.unpack_from("<Q", data, j)[0]
        j += 8
        triangles = struct.unpack_from("<H", data, j)[0]
        vertices = struct.unpack_from("<H", data, j + 2)[0]
        j += 4 + 4  # counts, data size
        stride = (desc & 0xF) * 4
        full = bool((desc >> 44) & 0x400) or bs_version == 100
        points = []
        for v in range(vertices):
            at = j + v * stride
            if full:
                points.append(struct.unpack_from("<3f", data, at))
            else:
                points.append(struct.unpack_from("<3e", data, at))
        yield kind, translation, points


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    command, archive = sys.argv[1], Archive(sys.argv[2])
    if command == "list":
        pattern = re.compile(sys.argv[3] if len(sys.argv) > 3 else ".", re.I)
        for name in sorted(archive.files):
            if pattern.search(name):
                print(name)
    elif command == "bounds":
        for name in sys.argv[3:]:
            print(name)
            for kind, translation, points in nif_shapes(archive.read(name)):
                lo = [min(p[k] for p in points) for k in range(3)]
                hi = [max(p[k] for p in points) for k in range(3)]
                size = [h - l for h, l in zip(hi, lo)]
                print(f"  {kind}: {len(points)} vertices, at {tuple(round(t, 1) for t in translation)}, "
                      f"x {lo[0]:.1f}..{hi[0]:.1f}  y {lo[1]:.1f}..{hi[1]:.1f}  z {lo[2]:.1f}..{hi[2]:.1f}  "
                      f"(size {size[0]:.0f} x {size[1]:.0f} x {size[2]:.0f} units)")
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
