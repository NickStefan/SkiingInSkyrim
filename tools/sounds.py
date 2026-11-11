#!/usr/bin/env python3
"""Turn the recordings in assets/sounds/<set>/ into the mod's sound files.

    python3 tools/sounds.py [OUTPUT]     default build/mod

Each recording (any format ffmpeg reads: m4a, wav, flac, mp3) is cut into separate sounds wherever it goes
quiet, so a take of twenty brushes with a pause between each gives twenty sounds. Each is filtered, levelled to
the same loudness, faded at both ends and written as 16-bit mono 44.1 kHz WAV (what Skyrim plays) to
OUTPUT/Sound/FX/SkiingInSkyrim/<set>/. make_plugin lists them in the set's sound descriptor; the game picks one
at random each time.
"""
from array import array
from pathlib import Path
import math
import subprocess
import sys
import wave

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / 'assets/sounds'
FOLDER = Path('Sound/FX/SkiingInSkyrim')
GAME_FOLDER = 'Data\\Sound\\FX\\SkiingInSkyrim'
RATE = 44100
WINDOW = RATE // 100  # 10 ms
AUDIO = {'.wav', '.m4a', '.mp3', '.flac', '.ogg', '.aac', '.caf', '.aif', '.aiff'}

# Per set: the longest a sound may be (seconds) and its loudness (RMS of its loud part, dBFS). Optionally the
# set whose recordings it uses ('source') and a pitch factor (below 1 is lower, and as much longer).
SETS = {
    'powder-turn': {'max_seconds': 1.5, 'level_db': -25.0, 'source': 'packed-turn'},
    'powder-skid': {'max_seconds': 2.0, 'level_db': -23.0, 'source': 'packed-skid'},
    'packed-turn': {'max_seconds': 1.5, 'level_db': -20.0},
    'packed-skid': {'max_seconds': 2.0, 'level_db': -18.0},
}
FILTERS = 'highpass=f=70,lowpass=f=14000,afftdn=nr=12:nf=-70:tn=1'
SKIP_START = 0.5    # seconds at each end of a take left out: the phone's record and stop taps
SKIP_END = 0.7
QUIET_DB = 40.0     # below the take's loudest by this much counts as a pause
ABOVE_FLOOR = 2.0   # and at least this many times the room noise (6 dB)
CLEAR_DB = 15.0     # sounds less than this far above the room noise are left out: they'd hiss once levelled
JOIN_SECONDS = 0.15 # pauses shorter than this don't split a sound
SHORTEST = 0.12     # sounds shorter than this are dropped (clicks, bumps)
LEAD = 0.04         # kept before each sound starts
TAIL = 0.15         # and after it ends
FADE_IN = 0.008
FADE_OUT = 0.06
CUT_FADE_OUT = 0.25  # a sound cut short at its set's longest fades out over this instead
PEAK_DB = -1.0


def decode(path, pitch=1.0):
    filters = FILTERS
    if pitch != 1.0:
        filters += f',aresample={RATE},asetrate={round(RATE * pitch)},aresample={RATE}'
    # Some iPhone ALAC files end in an empty frame that ffmpeg reports as an error; the audio before it is fine.
    result = subprocess.run(['ffmpeg', '-v', 'fatal', '-i', str(path), '-af', filters, '-ac', '1', '-ar', str(RATE),
                             '-f', 'f32le', '-'], capture_output=True, check=True)
    samples = array('f')
    samples.frombytes(result.stdout)
    return samples


def windows(samples):
    return [math.sqrt(sum(x * x for x in samples[i:i + WINDOW]) / WINDOW)
            for i in range(0, len(samples) - WINDOW + 1, WINDOW)]


def sounds_in(samples):
    """(start, end) sample ranges of the separate sounds in a take, and how far (dB) each is above the room
    noise."""
    skip_start, skip_end = int(SKIP_START * RATE) // WINDOW, int(SKIP_END * RATE) // WINDOW
    levels = windows(samples)
    if len(levels) <= skip_start + skip_end:
        return []
    levels = [0.0] * skip_start + levels[skip_start:len(levels) - skip_end] + [0.0] * skip_end
    kept = levels[skip_start:len(levels) - skip_end]
    loudest = max(kept)
    floor = max(sorted(kept)[len(kept) // 10], 1e-9)
    threshold = max(loudest * 10 ** (-QUIET_DB / 20), floor * ABOVE_FLOOR)
    ranges = []
    for i, level in enumerate(levels):
        if level < threshold:
            continue
        if ranges and (i - ranges[-1][1]) * 0.01 <= JOIN_SECONDS:
            ranges[-1][1] = i + 1
        else:
            ranges.append([i, i + 1])
    found = []
    for start, end in ranges:
        if (end - start) * 0.01 < SHORTEST:
            continue
        clear = 20 * math.log10(max(levels[start:end]) / floor)
        found.append((max(0, start * WINDOW - int(LEAD * RATE)), min(len(samples), end * WINDOW + int(TAIL * RATE)),
                      clear))
    return found


def shaped(samples, max_seconds, level_db):
    cut = len(samples) > int(max_seconds * RATE)
    clip = list(samples[:int(max_seconds * RATE)])
    mean = sum(clip) / len(clip)
    clip = [x - mean for x in clip]
    levels = sorted(windows(clip), reverse=True)
    loud = levels[:max(1, len(levels) // 2)]
    rms = math.sqrt(sum(level * level for level in loud) / len(loud)) or 1e-9
    gain = 10 ** (level_db / 20) / rms
    peak = max(abs(x) for x in clip) * gain
    gain *= min(1.0, 10 ** (PEAK_DB / 20) / max(peak, 1e-9))
    fade_in, fade_out = int(FADE_IN * RATE), int((CUT_FADE_OUT if cut else FADE_OUT) * RATE)
    for i in range(min(fade_in, len(clip))):
        clip[i] *= 0.5 - 0.5 * math.cos(math.pi * i / fade_in)
    for i in range(min(fade_out, len(clip))):
        clip[-1 - i] *= 0.5 - 0.5 * math.cos(math.pi * i / fade_out)
    return [x * gain for x in clip]


def write_wav(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    pcm = array('h', (max(-32768, min(32767, round(x * 32767))) for x in samples))
    with wave.open(str(path), 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(pcm.tobytes())


def build(output):
    """Writes every set's sounds under output; returns {set: [game paths]} for the sets that have any."""
    made = {}
    for name, options in SETS.items():
        options = dict(options)
        folder, pitch = options.pop('source', name), options.pop('pitch', 1.0)
        sources = sorted(p for p in (ASSETS / folder).glob('*') if p.suffix.lower() in AUDIO)
        paths = []
        for source in sources:
            samples = decode(source)
            found = sounds_in(samples)
            clear = [(start, end) for start, end, db in found if db >= CLEAR_DB]
            if pitch != 1.0:
                # Cut at the same places as the unpitched set, so both have the same sounds.
                samples = decode(source, pitch)
                clear = [(round(start / pitch), min(len(samples), round(end / pitch))) for start, end in clear]
            if not clear:
                print(f'warning: {source.relative_to(ROOT)}: no clear sound found (too quiet? record closer)')
            elif len(clear) < len(found):
                print(f'{source.relative_to(ROOT)}: left out {len(found) - len(clear)} faint sound'
                      f'{"s" if len(found) - len(clear) != 1 else ""} (under {CLEAR_DB:.0f} dB above the room noise)')
            for start, end in clear:
                file = f'{name.replace("-", "_")}_{len(paths) + 1:02d}.wav'
                write_wav(output / FOLDER / name / file, shaped(samples[start:end], **options))
                paths.append(f'{GAME_FOLDER}\\{name}\\{file}')
        if paths:
            made[name] = paths
            print(f'Sounds: {name}: {len(paths)} from {len(sources)} recording{"s" if len(sources) != 1 else ""}'
                  + (f' in {folder}' if folder != name else '') + (f' at pitch {pitch:g}' if pitch != 1.0 else ''))
    return made


def main():
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build/mod'
    made = build(output)
    if not made:
        print(f'No recordings in {ASSETS.relative_to(ROOT)}/<set>/ ({", ".join(SETS)})')


if __name__ == '__main__':
    main()
