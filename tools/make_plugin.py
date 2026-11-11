#!/usr/bin/env python3
"""Write SkiingInSkyrim.esp (ESL-flagged): the skis, their keyword, a forge recipe and the ski sounds' descriptors.

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
# Sound descriptors, one per set made by tools/sounds.py.
SOUNDS = {
    'packed-turn': (0x01000804, 'SkiingInSkyrim_PackedTurnSD'),
    'packed-skid': (0x01000805, 'SkiingInSkyrim_PackedSkidSD'),
    'powder-turn': (0x01000806, 'SkiingInSkyrim_PowderTurnSD'),
    'powder-skid': (0x01000807, 'SkiingInSkyrim_PowderSkidSD'),
}
NEXT_OBJECT = 0x808

# Skyrim.esm
DEFAULT_RACE = 0x00000019
CRAFTING_SMITHING_FORGE = 0x00088105
FIREWOOD = 0x0006F993
LEATHER_STRIPS = 0x000800E4
AUDIO_CATEGORY_SFX = 0x000172A1  # the game's effects volume slider
SOM_MONO_01400_PLAYER_1ST = 0x000B4058  # what the player's own footsteps play through
STANDARD_SOUND = 0x1EEF540A  # CNAM: a standard sound descriptor

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
HEADER_VERSION = 1.70  # 1.71 needs AE; SE 1.5.97 only loads ESLs up to 1.70 (ids 0x800-0xFFF)


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


def sound_descriptor(form_id, editor_id, files):
    # BNAM: frequency shift and variance, priority, dB variance, static attenuation (1/100 dB). The DLL varies
    # pitch and volume itself.
    return record(
        'SNDR', form_id,
        sub('EDID', zstring(editor_id)),
        sub('CNAM', struct.pack('<I', STANDARD_SOUND)),
        sub('GNAM', struct.pack('<I', AUDIO_CATEGORY_SFX)),
        *(sub('ANAM', zstring(name)) for name in files),
        sub('ONAM', struct.pack('<I', SOM_MONO_01400_PLAYER_1ST)),
        sub('LNAM', struct.pack('<I', 1)),
        sub('BNAM', struct.pack('<bBBBH', 0, 0, 0x40, 0, 0)),
    )


def build(sounds=None):
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
    descriptors = [sound_descriptor(*SOUNDS[name], files) for name, files in (sounds or {}).items() if name in SOUNDS]
    groups = [group('KYWD', keyword), group('ARMO', skis), group('COBJ', recipe), group('ARMA', addon)]
    if descriptors:
        groups.append(group('SNDR', *descriptors))
    count = len(groups) + 4 + len(descriptors)
    header = record(
        'TES4', 0,
        sub('HEDR', struct.pack('<fiI', HEADER_VERSION, count, NEXT_OBJECT)),
        sub('CNAM', zstring('Nick')),
        sub('SNAM', zstring('Skiing in Skyrim: wearable skis. Needs SkiingInSkyrim.dll.')),
        sub('MAST', zstring('Skyrim.esm')),
        sub('DATA', struct.pack('<Q', 0)),
        flags=ESL,
    )
    return header + b''.join(groups)


def write(path, sounds=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(build(sounds))
    return path


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/mod' / NAME
    print(f'Wrote {write(path)}')


if __name__ == '__main__':
    main()
