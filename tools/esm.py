#!/usr/bin/env python3
"""List records in a Skyrim plugin (read only).

    python3 tools/esm.py sounds PLUGIN [PATTERN]    sound descriptors (SNDR): form id, editor id, category,
                                                    output model, files; PATTERN (regex) matches the editor id
                                                    or a file
    python3 tools/esm.py ids PLUGIN TYPE [PATTERN]  form id and editor id of each TYPE record (e.g. SNCT, SOPM)
"""
import re
import struct
import sys
import zlib
from pathlib import Path

COMPRESSED = 0x40000


def subrecords(data):
    at, size_override = 0, None
    while at + 6 <= len(data):
        kind, size = data[at:at + 4].decode('ascii', 'replace'), struct.unpack_from('<H', data, at + 4)[0]
        at += 6
        if kind == 'XXXX':
            size_override = struct.unpack_from('<I', data, at)[0]
            at += size
            continue
        if size_override is not None:
            size, size_override = size_override, None
        yield kind, data[at:at + size]
        at += size


def records(data, at=0, end=None):
    end = len(data) if end is None else end
    while at + 24 <= end:
        kind = data[at:at + 4].decode('ascii', 'replace')
        size = struct.unpack_from('<I', data, at + 4)[0]
        if kind == 'GRUP':
            yield from records(data, at + 24, at + size)
            at += size
            continue
        flags, form_id = struct.unpack_from('<II', data, at + 8)
        body = data[at + 24:at + 24 + size]
        if flags & COMPRESSED:
            body = zlib.decompress(body[4:])
        yield kind, form_id, body
        at += 24 + size


def text(value):
    return value.rstrip(b'\0').decode('cp1252', 'replace')


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    command, data = sys.argv[1], Path(sys.argv[2]).read_bytes()
    if command == 'sounds':
        pattern = re.compile(sys.argv[3] if len(sys.argv) > 3 else '.', re.I)
        for kind, form_id, body in records(data):
            if kind != 'SNDR':
                continue
            fields = list(subrecords(body))
            editor = next((text(v) for k, v in fields if k == 'EDID'), '')
            files = [text(v) for k, v in fields if k == 'ANAM']
            if not (pattern.search(editor) or any(pattern.search(f) for f in files)):
                continue
            category = next((struct.unpack('<I', v)[0] for k, v in fields if k == 'GNAM'), 0)
            output = next((struct.unpack('<I', v)[0] for k, v in fields if k == 'ONAM'), 0)
            loop = next((v for k, v in fields if k == 'BNAM'), b'')
            print(f'{form_id:08X} {editor}  category {category:08X} output {output:08X} values {loop.hex()}')
            for name in files:
                print(f'    {name}')
    elif command == 'ids':
        kind_wanted = sys.argv[3]
        pattern = re.compile(sys.argv[4] if len(sys.argv) > 4 else '.', re.I)
        for kind, form_id, body in records(data):
            if kind == kind_wanted:
                editor = next((text(v) for k, v in subrecords(body) if k == 'EDID'), '')
                if pattern.search(editor):
                    print(f'{form_id:08X} {editor}')
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
