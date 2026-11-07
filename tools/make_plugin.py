#!/usr/bin/env python3
"""Write SkiingInSkyrim.esp (ESL-flagged): the skis, their keyword and a forge recipe.

    python3 tools/make_plugin.py [OUTPUT]     default build/mod/SkiingInSkyrim.esp

The DLL turns skiing on while the player wears armour with the SkiingInSkyrim_Skis keyword and draws the
skis itself from the armour's world model, so the armour addon has no body model.
"""
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parent.parent
NAME = 'SkiingInSkyrim.esp'

# Our own records: master index 1 (after Skyrim.esm), object ids from 0x800 as an ESL requires.
KEYWORD = 0x01000800
SKIS = 0x01000801
SKIS_ADDON = 0x01000802
RECIPE = 0x01000803

# Skyrim.esm
DEFAULT_RACE = 0x00000019
CRAFTING_SMITHING_FORGE = 0x00088105
FIREWOOD = 0x0006F993
LEATHER_STRIPS = 0x000800E4

# Biped slot 60 (misc/FX): unused by vanilla gear, so boots still show.
SLOT = 60
WORLD_MODEL = 'Traps\\Boards\\BreakableBoard01.nif'  # stand-in until we have a ski mesh
BOUNDS = (-11, -136, -2, 11, 136, 2)
VALUE = 10
WEIGHT = 2.0
INGREDIENTS = [(FIREWOOD, 2), (LEATHER_STRIPS, 2)]

CLOTHING = 2  # BOD2 armour type
ESL = 0x200
FORM_VERSION = 44


def zstring(text):
    return text.encode('cp1252') + b'\0'


def sub(kind, data):
    return kind.encode() + struct.pack('<H', len(data)) + data


def record(kind, form_id, *subrecords, flags=0):
    data = b''.join(subrecords)
    return kind.encode() + struct.pack('<IIIIHH', len(data), flags, form_id, 0, FORM_VERSION, 0) + data


def group(kind, *records):
    data = b''.join(records)
    return b'GRUP' + struct.pack('<I4siHHI', 24 + len(data), kind.encode(), 0, 0, 0, 0) + data


def build():
    keyword = record('KYWD', KEYWORD, sub('EDID', zstring('SkiingInSkyrim_Skis')))
    slots = struct.pack('<II', 1 << (SLOT - 30), CLOTHING)
    skis = record(
        'ARMO', SKIS,
        sub('EDID', zstring('SkiingInSkyrim_SkisWooden')),
        sub('OBND', struct.pack('<6h', *BOUNDS)),
        sub('FULL', zstring('Wooden Skis')),
        sub('MOD2', zstring(WORLD_MODEL)),
        sub('BOD2', slots),
        sub('RNAM', struct.pack('<I', DEFAULT_RACE)),
        sub('KSIZ', struct.pack('<I', 1)),
        sub('KWDA', struct.pack('<I', KEYWORD)),
        sub('MODL', struct.pack('<I', SKIS_ADDON)),
        sub('DATA', struct.pack('<if', VALUE, WEIGHT)),
        sub('DNAM', struct.pack('<i', 0)),
    )
    recipe = record(
        'COBJ', RECIPE,
        sub('EDID', zstring('SkiingInSkyrim_RecipeSkisWooden')),
        sub('COCT', struct.pack('<I', len(INGREDIENTS))),
        *(sub('CNTO', struct.pack('<Ii', item, count)) for item, count in INGREDIENTS),
        sub('CNAM', struct.pack('<I', SKIS)),
        sub('BNAM', struct.pack('<I', CRAFTING_SMITHING_FORGE)),
        sub('NAM1', struct.pack('<H', 1)),
    )
    addon = record(
        'ARMA', SKIS_ADDON,
        sub('EDID', zstring('SkiingInSkyrim_SkisWoodenAA')),
        sub('BOD2', slots),
        sub('RNAM', struct.pack('<I', DEFAULT_RACE)),
        sub('DNAM', bytes(12)),
    )
    groups = [group('KYWD', keyword), group('ARMO', skis), group('COBJ', recipe), group('ARMA', addon)]
    count = len(groups) + 4
    header = record(
        'TES4', 0,
        sub('HEDR', struct.pack('<fiI', 1.71, count, 0x804)),
        sub('CNAM', zstring('Nick')),
        sub('SNAM', zstring('Skiing in Skyrim: wearable skis. Needs SkiingInSkyrim.dll.')),
        sub('MAST', zstring('Skyrim.esm')),
        sub('DATA', struct.pack('<Q', 0)),
        flags=ESL,
    )
    return header + b''.join(groups)


def write(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(build())
    return path


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/mod' / NAME
    print(f'Wrote {write(path)}')


if __name__ == '__main__':
    main()
