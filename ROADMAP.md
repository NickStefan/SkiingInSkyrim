# Roadmap

Rough order of work. Things move up or down as playtesting shows what matters.

## Done

- Ski model: yaw separate from travel, edge grip and sidecut carving, skids, parking, falling leaf.
- Camera-relative steering on the left stick (right stick optional), tuck, self-aligning skis.
- Powder setting, softened fall damage, steep-slope height jitter fix.
- Debug overlay: fall line, ski and travel arrows, edge gauge, pivot and lean.

## Now: speed

- [ ] Powder drag grows with the square of speed, so gentle runs keep momentum and steep runs are still held back.
      Playtest the feel; tune `PowderDragPerSpeedSquared`.
- [ ] Carve pump: a clean carve held through a turn keeps or builds speed. Tune `CarvePump`.
- [ ] Skating on flat and gentle uphill ground: stick forward, alternate trigger squeezes. Tune `SkateImpulse`,
      `SkateMaxSpeed`, `SkateMaxSlopeDegrees`.
- [ ] Cross-country feel: whether skating plus gentle-ground glide is enough to travel Skyrim on skis, or whether
      longer flats need a dedicated stride/double-pole move.

## Next: looking like skiing

- [ ] Body animation: lean into carves, forward lean when tucking, knee bend, skid stance.
- [ ] Pole plants at turn transitions and while skating.
- [ ] Skating stride state for animation (which leg, progress through the stride), with the stride's push spread
      over the leg's push phase instead of landing in one frame.
- [ ] Skis as a wearable item: an ARMO/ARMA record the player equips; wearing it turns ski mode on, instead of
      the hotkey toggle. Needs ski meshes.
- [ ] Sounds: edge scrape that grows with skidding, carve hiss with speed, powder whoosh, landings.

## Later / ideas

- [ ] Detect snowy ground (and deep snow from snow-deformation mods) to switch powder on and off automatically.
      Weigh against compatibility.
- [ ] Hops by pumping the stick (unweighting).
- [ ] Revisit fall damage if landings still feel punishing.
- [ ] Camera comfort: options that reduce motion sickness on long runs.
- [ ] NPC skiers (the plugin is player-only for now).
- [ ] Publish the repository.
