# Ski sounds

Drop recordings into a set's folder and build: `tools/sounds.py` (run by `tools/build.py`) cuts each recording
into separate sounds wherever it goes quiet, filters and levels them, and writes them into the mod with a sound
descriptor in the ESP. The game picks one at random each time and the DLL varies its pitch and volume a little.
Any format ffmpeg reads works (iPhone m4a, wav, flac, mp3).

- `powder-turn/`, `packed-turn/`: one sound as each turn starts and at each change of direction. Short, 0.3 to
  1 s.
- `powder-skid/`, `packed-skid/`: skids and hockey stops; a long skid plays another every 0.7 s or so. 0.5 to
  2 s.

On any ground, each turn or skid plays the powder and packed sounds together, their volumes split by how deep
the powder is (all powder off the roads in deep snow, all packed on roads, paths and bare ground, both in
between). A set with no recordings borrows the other's; with neither, the game's sneaking footsteps stand in.
For skating strides, landings and crashes, the game's own sounds for the ground are used.

A set can take another's recordings instead of its own (`source` in `SETS`, `tools/sounds.py`): the powder sets
are currently the packed recordings, quieter.

## Recording (foley)

Powder is close to lightly brushing a fleece blanket; cornstarch squeezed in a pouch is the classic crunching
snow.

- Quiet room with soft furnishings (a bedroom or a closet of clothes); fans, heating and the fridge off.
- iPhone Voice Memos: Settings, Apps, Voice Memos, Audio Quality: Lossless; turn "Enhance Recording" off.
- Phone lying still on a towel, its bottom edge (the microphone) 10 to 20 cm from the fabric. Don't touch it.
- One gesture at a time with a second of silence between, so each becomes its own sound. Twenty or more per
  set, varying speed and pressure: the more variety, the less it repeats.
- Turns: smooth sweeps of the palm across the fleece, 0.3 to 1 s, light to medium pressure.
- Skids: firmer, longer drags with the fingers or palm, 0.5 to 2 s, building then easing off; a few short hard
  ones for hockey stops.
- Loud enough to be clear, never clipping. A few seconds of room silence at the start helps.

## Other people's sounds

Note each one here and in `dist/THIRD-PARTY-NOTICES.md` (shipped with the mod) before building a release:
title, author, link, licence, and that it was trimmed and processed. CC0 and CC BY are fine; avoid
NonCommercial (NC) and NoDerivatives (ND).
