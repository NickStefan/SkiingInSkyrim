# Roadmap

Rough order of work. Things move up or down as playtesting shows what matters.

## Done

- Ski model: yaw separate from travel, edge grip and sidecut carving, skids, parking, falling leaf.
- Camera-relative steering on the left stick or W/A/S/D (looking around steers too: on by default for
  keyboard/mouse, off for gamepad), tuck, self-aligning skis.
- Speed: two-part drag (light air drag plus a speed limiter), so gentle runs carry speed and steep ones don't run
  away; powder adds to the limiter.
- Carve pump: a clean carve held through a turn keeps or builds speed, never more than cancelling drag.
- Skating on flat and gentle uphill ground: stick forward, alternate trigger squeezes.
- Powder setting, softened fall damage, steep-slope height jitter fix.
- Debug overlay: fall line, ski and travel arrows, edge gauge, pivot, lean, skating, mph.

Controller is the intended way to play; keyboard and mouse work but are rougher (see Later).

## Next: publish

- [ ] Push the repository to GitHub.
- [ ] README: what the mod does, controls (controller first), requirements, install, "works best on controller".

## Then: looking like skiing

Order within this phase is still to be planned. The wearable item likely comes first, because animation and
sound conditions can key off it.

- [ ] Skis as a wearable item: an ARMO/ARMA record the player equips; wearing it turns ski mode on, instead of
      the hotkey toggle. Needs a plugin file (ESP/ESL) and ski meshes.
- [ ] Body animation: lean into carves, forward lean when tucking, knee bend, skid stance.
- [ ] Pole plants at turn transitions and while skating.
- [ ] Skating stride state for animation (which leg, progress through the stride), with the stride's push spread
      over the leg's push phase instead of landing in one frame.
- [ ] Sounds: edge scrape that grows with skidding, carve hiss with speed, powder whoosh, landings.

## Later / ideas

- [ ] Carving costs stamina: holding hard edges drains it, so long aggressive runs need pacing.
- [ ] Cross-country feel: whether skating plus gentle-ground glide is enough to travel Skyrim on skis, or whether
      longer flats need a dedicated stride/double-pole move.
- [ ] Auto-edge mode (keyboard-only skiing with WASD): the edge follows the steering intent instead of the mouse
      buttons. Turning edges toward the turn (more with a bigger aim offset and more speed), traversing across the
      hill holds a light uphill edge so the line doesn't slide away, pointing downhill or tucking (W) runs flat, S
      brakes. Simpler than the controller's manual edging (no falling leaf or sudden hard edge), but should make
      cat tracks and switchbacks workable without analog triggers.
- [ ] Detect snowy ground (and deep snow from snow-deformation mods) to switch powder on and off automatically.
      Weigh against compatibility.
- [ ] Powder planing: in deep (snow-deformation) powder the skis, and the body with them, ride deep when slow and
      rise toward the snow surface with speed. Skis are placed by the DLL, so the height is ours to set; the body
      needs a visual-only lift that doesn't disturb the height smoothing. Pairs with snowy-ground detection.
- [ ] Hops by pumping the stick (unweighting).
- [ ] Revisit fall damage if landings still feel punishing.
- [ ] Camera comfort: options that reduce motion sickness on long runs.
- [ ] NPC skiers (the plugin is player-only for now).

## Last

- [ ] Ski-run quests: start at marker A, pass markers B, C, ... in order; succeed at the last marker, fail if the
      skis are taken off (Papyrus OnObjectUnequipped / item removed) before reaching the next one.
