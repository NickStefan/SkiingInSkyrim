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
- Powder setting, steep-slope height jitter fix.
- Skiing stops (skis stay on) during conversations, including NPCs starting one, and while downed
  (bleedout, including revive mods that only play the animation).
- Impact damage instead of the game's fall damage on skis (which counted steep descents as one long fall and
  hurt soft landings on steep faces): landings hurt by the speed the air added into the slope at touchdown, so a
  big air onto a steep face going downhill is soft, and so is a hop into a ramp skied into anyway; crashes into trees, rocks and walls hurt by the speed lost sideways.
  Both on the game's fall-damage curve, with damage sliders. Landing on a steep face turns some of the fall
  into downhill speed (a slider).
- Tumbling: a big enough landing or crash throws you into the game's ragdoll on the way you were going, and the
  skis come off (kept in the inventory) if it hurt; harmless tumbles keep them on by default. All optional. Sliders set how much speed the body keeps into the
  tumble and how long it tumbles before it's braked to a stop; no fall damage counts until you're back on your
  feet.
- Skis come off in water (kept in the inventory).
- Skis as a wearable, craftable item (ESP); wearing them turns skiing on. Ski models placed under the feet.
- Procedural ski stance, no animation files: knee bend, forward lean, tuck, lean into turns with hip angulation,
  leg IK to the skis, skating strides (V stance, edged push, weight shift, arm swing). HDT-SMP safe.
- Skid stance: skidding, the upper body turns toward the direction of travel while the legs stay with the skis.
- Airs: small hops keep the stance; past a set drop the game's (or OAR's) fall animation takes over.
- Stamina: hard carving and skating strides cost stamina; out of it, edges are weaker and strides don't start.
- Debug overlay: fall line, ski and travel arrows, edge gauge, slope, skating, mph.
- Loads on SE 1.5.97 and AE (Address Library); refuses to load on VR.
- Settings menu in pages: General, Controls, Speed, Body, Camera, Stamina, Impacts.

Controller is the intended way to play; keyboard and mouse work but are rougher. True Directional Movement is a
soft requirement (see Later).

## Next: release

- [ ] Footsteps: check whether the walk/run animation under the stance still plays footstep sounds; silence them.
- [ ] Sounds: edge scrape that grows with skidding, carve hiss with speed, powder whoosh, landings. Pick them by
      surface once surfaces (see Later) exist.
- [ ] README: what the mod does, controls (controller first), requirements (TDM recommended), install, "works best
      on controller".
- [ ] Licenses in the Nexus download: the upload is just build/mod/ (the DLL, PDB and ESP), but the libraries
      compiled into the DLL (MIT and similar) require their license text to travel with the binary, so the
      download itself has to carry it. Work out which apply (CommonLibSSE-NG and its bundled hde64, spdlog, fmt,
      the SKSE Menu Framework header, the TDM API header, DirectXMath/DirectXTK if any of it ends up in the DLL)
      and have the build copy those license texts plus our own LICENSE into build/mod/ (e.g. a licenses folder,
      as other CommonLibSSE-NG mods ship), so whatever gets zipped for Nexus already includes them.
- [ ] Push the repository to GitHub.

## Later / ideas

- [ ] Ski poles, then pole plants at turn transitions and while skating.
- [ ] Landing absorption: the legs soak up the landing. Optional; powder already hides most small landings.
- [ ] Surfaces: skis work best on snow but never stop working. Extra drag per material on top of the usual drag,
      so steepness still wins: snow as now; ice faster with weaker edges; grass and mud slower; dirt, sand and
      gravel slower still; stone and wood (roads, towns) heavy, mostly pushing on the flat. Unknown material counts
      as snow; blend changes over a short distance; keep the last surface in the air. One setting, "non-snow
      friction" (0 = surfaces ignored). Later: powder only on snow, stone scrape vs snow hiss sounds, skating on
      grass costs more. Show the material underfoot on the overlay first to check readings, Seasons included.
      The character controller's surfaceMaterial (what footsteps use) gives the material underfoot, rocks
      included; TES::GetLandMaterialType gives the landscape's at any point. Don't look materials up by shape
      key: that crashed inside compressed meshes.
- [ ] Deep snow: detect it from snow-deformation mods to switch powder on and off automatically. Weigh against
      compatibility.
- [ ] Powder planing: in deep (snow-deformation) powder the skis, and the body with them, ride deep when slow and
      rise toward the snow surface with speed. Skis are placed by the DLL, so the height is ours to set; the body
      needs a visual-only lift that doesn't disturb the height smoothing. Pairs with snowy-ground detection.
- [ ] First person: turning doesn't work. TDM accepts our yaw but only applies it in its third-person camera,
      and we zero the game's rotation, so look turning is dropped too. Setting the heading directly fixes turning
      but breaks third person (it skips TDM's camera counter-rotation, which the chase camera relies on), and the
      view then swings with every carve and skate push. Needs its own camera: the view lags and settles toward
      travel, torso facing downhill, rather than following the skis.
- [ ] Vanilla third person (no TDM) is untested: while moving it ties the view to the body like first person, so
      expect the same issues.
- [ ] NPC skiers (the plugin is player-only for now).
- [ ] NPC crashes: skiing into people counts too. The crash ray already sees them (TESHavokUtilities::
      FindCollidableRef gives the actor); log first, since two character controllers meeting may push each other
      aside rather than stop dead. Decide what the NPC gets (damage, stagger, knockdown) and make sure it isn't
      assault: hitting a guard shouldn't start a fight or a bounty.

## Last

- [ ] Ski-run quests: start at marker A, pass markers B, C, ... in order; succeed at the last marker, fail if the
      skis are taken off (Papyrus OnObjectUnequipped / item removed) before reaching the next one.
