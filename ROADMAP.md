# Roadmap

Rough order of work. Things move up or down as playtesting shows what matters.

## Done

- Ski model: yaw separate from travel, edge grip and sidecut carving, skids, parking, falling leaf.
- Camera-relative steering on the left stick or W/A/S/D (looking around steers too: on by default for
  keyboard/mouse, off for gamepad), tuck, self-aligning skis.
- Speed: two-part drag (light air drag plus a speed limiter), so gentle runs carry speed and steep ones don't run
  away; powder adds to the limiter.
- Carve pump: a clean carve held through a turn keeps or builds speed, never more than cancelling drag.
- Skating on flat and gentle uphill ground: stick forward, alternate trigger squeezes, push spread over each stride.
- Powder setting, softened fall damage, steep-slope height jitter fix.
- Skis as a wearable, craftable item (ESP); wearing them turns skiing on. Ski models placed under the feet.
- Procedural ski stance, no animation files: knee bend, forward lean, tuck, lean into turns with hip angulation,
  leg IK to the skis, skating strides (V stance, edged push, weight shift, arm swing). HDT-SMP safe.
- Debug overlay: fall line, ski and travel arrows, edge gauge, pivot, lean, skating, mph.

Controller is the intended way to play; keyboard and mouse work but are rougher. True Directional Movement is a
soft requirement (see Later).

## Next: release

- [ ] Skid stance: upper body faces downhill while the skis skid across.
- [ ] Airborne pose: the stance overrides the game's (and OAR's) falling animation. Keep a compact pose for small
      hops; on longer or higher airs, arms out and more upright, or fade to the game's fall animation.
- [ ] Carving and skating cost stamina: holding hard edges and skating strides drain it, so long aggressive runs
      and climbs need pacing.
- [ ] Footsteps: check whether the walk/run animation under the stance still plays footstep sounds; silence them.
- [ ] Sounds: edge scrape that grows with skidding, carve hiss with speed, powder whoosh, landings.
- [ ] README: what the mod does, controls (controller first), requirements (TDM recommended), install, "works best
      on controller".
- [ ] Push the repository to GitHub.

## Later / ideas

- [ ] Ski poles, then pole plants at turn transitions and while skating.
- [ ] Landing absorption: the legs soak up the landing. Optional; powder already hides most small landings.
- [ ] Detect snowy ground (and deep snow from snow-deformation mods) to switch powder on and off automatically.
      Weigh against compatibility.
- [ ] Powder planing: in deep (snow-deformation) powder the skis, and the body with them, ride deep when slow and
      rise toward the snow surface with speed. Skis are placed by the DLL, so the height is ours to set; the body
      needs a visual-only lift that doesn't disturb the height smoothing. Pairs with snowy-ground detection.
- [ ] Revisit fall damage if landings still feel punishing.
- [ ] First person: turning doesn't work. TDM accepts our yaw but only applies it in its third-person camera,
      and we zero the game's rotation, so look turning is dropped too. Setting the heading directly fixes turning
      but breaks third person (it skips TDM's camera counter-rotation, which the chase camera relies on), and the
      view then swings with every carve and skate push. Needs its own camera: the view lags and settles toward
      travel, torso facing downhill, rather than following the skis.
- [ ] Vanilla third person (no TDM) is untested: while moving it ties the view to the body like first person, so
      expect the same issues.
- [ ] NPC skiers (the plugin is player-only for now).

## Last

- [ ] Ski-run quests: start at marker A, pass markers B, C, ... in order; succeed at the last marker, fail if the
      skis are taken off (Papyrus OnObjectUnequipped / item removed) before reaching the next one.
