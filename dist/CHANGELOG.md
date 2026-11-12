# Changelog

## Unreleased

- God mode (`tgm`) is respected: no crash or landing damage, no tumbles, and skiing costs no stamina. The Flow bar
  isn't spent while it's on.

## 0.1.0

First release.

- Wearable, craftable Wooden Skis: skiing turns on while they're worn.
- Carving on the edges, skidding, tucking and skating on the flats, steered relative to the camera.
- Speed that follows the slope: gentle runs carry speed, steep ones don't run away.
- Powder off roads and paths, detected from the landscape: slower than packed snow, and softer to land in.
- A procedural ski stance: knees, lean, angulation into turns, skating strides.
- A chase camera that follows the direction you're travelling.
- Impacts: crashes and flat landings hurt in place of the game's fall damage, and big ones throw you into a
  tumble, filmed by a steady tumble camera (without the game's droning knockdown sound).
- Flow (on by default, General page): ski down without falling to fill a bar in the corner; once it's full, it
  takes the hits from big airs and crashes until it's spent. Not kept in your save.
- Stamina for hard carving and skating.
- Ski sounds: turns, skids and hockey stops (recorded foley, softer in powder), skating strides, landings and
  crashes. Running straight is quiet, and the walking footsteps under the ski stance are silenced.
- Settings in the SKSE Menu Framework menu, with an optional debug overlay (off by default).
