#!/usr/bin/env python3
"""Take a steady hum (and its other tones) out of a recording, leaving the rest of the sound as it was.

    python tools/dehum.py INPUT OUTPUT.flac

Needs numpy and scipy, which the build doesn't, so it's run by hand and its output kept in assets/sounds; e.g.
from a venv under build/ (python3 -m venv build/audio-venv; build/audio-venv/bin/pip install numpy scipy).

Tones are the frequencies that stand out from their ~60 Hz neighbourhood in a good share of the recording's 4 s
slices. In every short frame each of them is pulled down to the median of the frequencies around it, so the hum
goes but a skid's own noise at that frequency stays. Writes 24-bit mono 44.1 kHz FLAC.
"""
import subprocess
import sys

import numpy as np
import scipy.signal as sig

RATE = 44100
FRAME = 8192            # samples per analysis frame: 5.4 Hz apart, fine enough to pick out a tone
SLICE = 4 * RATE
OVER_DB = 5.0           # a tone stands at least this far above its neighbourhood
IN_SHARE = 0.3          # in at least this share of the slices
LOWEST, HIGHEST = 80.0, 5000.0
NEIGHBOURS = 20         # bins either side (~110 Hz) whose median a tone is pulled down to


def decode(path):
    out = subprocess.run(['ffmpeg', '-v', 'fatal', '-i', str(path), '-ac', '1', '-ar', str(RATE), '-f', 'f32le', '-'],
                         capture_output=True).stdout
    return np.frombuffer(out, np.float32).astype(np.float64)


def tones(x):
    hits = {}
    starts = range(0, len(x) - SLICE + 1, SLICE)
    for start in starts:
        f, p = sig.welch(x[start:start + SLICE], RATE, nperseg=2 * FRAME)
        db = 10 * np.log10(p + 1e-30)
        for i in np.where((db - sig.medfilt(db, 21) > OVER_DB) & (f > LOWEST) & (f < HIGHEST))[0]:
            hits[f[i]] = hits.get(f[i], 0) + 1
    return sorted(hz for hz, n in hits.items() if n >= IN_SHARE * len(starts))


def dehum(x, found):
    f = np.fft.rfftfreq(FRAME, 1 / RATE)
    tonal = np.zeros(len(f), bool)
    for hz in found:
        tonal |= abs(f - hz) < 4
    tonal = np.convolve(tonal, np.ones(3), 'same') > 0
    _, _, z = sig.stft(x, RATE, nperseg=FRAME, noverlap=FRAME * 3 // 4)
    mag = np.abs(z)
    out = mag.copy()
    for i in np.where(tonal)[0]:
        near = [j for j in range(i - NEIGHBOURS, i + NEIGHBOURS + 1) if 0 <= j < len(f) and not tonal[j]]
        out[i] = np.minimum(mag[i], np.median(mag[near], axis=0))
    _, y = sig.istft(out * np.exp(1j * np.angle(z)), RATE, nperseg=FRAME, noverlap=FRAME * 3 // 4)
    return y[:len(x)]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    x = decode(sys.argv[1])
    found = tones(x)
    print('tones (Hz):', ' '.join(f'{hz:.0f}' for hz in found) or 'none')
    y = dehum(x, found).astype(np.float32)
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'f32le', '-ar', str(RATE), '-ac', '1', '-i', '-',
                    '-c:a', 'flac', '-sample_fmt', 's32', '-bits_per_raw_sample', '24', sys.argv[2]],
                   input=y.tobytes(), check=True)


if __name__ == '__main__':
    main()
