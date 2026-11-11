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
- Powder detection ("Detect snow"): powder only where the landscape is snow, off road, path and trail textures,
  and not on rocks or other objects. Read from the game's own landscape data (a 3x3 grid of points around the
  feet, a ray down for the landscape), so it's the same with or without snow-deformation mods. Blended over 3 m,
  snapped on touchdown so landings get the right cushion.
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
  feet. A tumble shot in place of what the game's knockdown camera shows: frozen where
  the camera was at the hit, then drifting lazily after the body without turning with it, aimed at a
  smoothed point on the body and kept above the ground, through getting up; then back to third person, facing
  the same way. The game stays in its knockdown (bleedout) camera state, which SmoothCam leaves alone; we hook
  that state's Update/GetRotation/GetTranslation after every plugin's post-load, so Enhanced Death Cam's own hook
  on it (used with SmoothCam) runs first. Forcing third person fought SmoothCam (spinning, cuts), and the free
  camera stops the player updating. Enhanced Death Cam's ragdoll camera without SmoothCam keeps third person, so
  no tumble shot: shown in the menu's Compatibility section. Bleeding out, the shot stops with the ragdoll.
  A ray from the body to the camera (on the game camera's collision layer) brings the camera in front of rocks
  and slopes in the way at once, easing back out once clear.
  Skiing (and the chase camera) waits for the get-up animation to finish: it turns the character, which the
  chase camera read as looking around and spun with.
  A body creeping down the slope is braked to rest so the game gets you up.
- Flow (General page, on by default): descending on skis without a tumble fills a bar
  in the top right corner (white; 150 m by default). Full (gold), landings and crashes that would hurt or knock
  you down are taken from its reserve (25 m of fall by default) instead; the last one is let off however big,
  and empties it. A tumble while filling starts it over. Height counts air drops too; fast travel and load
  doors keep it (teleport frames don't count), a save loading or a new game empties it: nothing goes into the
  save. Shown only with the skis on and no menu open, independent of the debug overlay. The same chime as it
  fills and as it empties (UIEnchantingLearnEffect). core/Flow, FlowBar.
- Skis come off in water (kept in the inventory).
- Skis as a wearable, craftable item (ESP); wearing them turns skiing on. Ski models placed under the feet.
- Procedural ski stance, no animation files: knee bend, forward lean, tuck, lean into turns with hip angulation,
  leg IK to the skis, skating strides (V stance, edged push, weight shift, arm swing). HDT-SMP safe.
- Skid stance: skidding, the upper body turns toward the direction of travel while the legs stay with the skis.
- Airs: small hops keep the stance; past a set drop the game's (or OAR's) fall animation takes over.
- Stamina: hard carving and skating strides cost stamina; out of it, edges are weaker and strides don't start.
- Debug overlay (off by default): fall line, ski and travel arrows, edge gauge, slope, skating, mph, land material
  and powder.
- Log: by default only start-up, compatibility, state changes, crashes, big landings and tumbles. "Detailed
  log" (General page, off by default) adds telemetry, surfaces, airs, every impact and the tumble camera
  (about 7.5 MB an hour, three quarters of it telemetry).
- Loads on SE 1.5.97 and AE (Address Library); refuses to load on VR.
- Sounds: the player's footsteps silenced while skiing (BGSImpactManager's footstep sink skips the player).
  Event sounds only (core/SkiSound): a turn sound as the path starts curving each way, skids while sliding
  sideways (repeating), skating strides, landings and crashes. Running straight is quiet. Turns and skids are
  our recordings on every ground (Nick's foley in assets/sounds; tools/sounds.py slices, filters and levels
  them, make_plugin adds a SNDR per set, tools/bsa.py packs them uncompressed into SkiingInSkyrim.bsa).
  Powder and packed sets play together, split by the powder share; powder is currently the packed recordings,
  5 dB quieter (pitched down and Nick's own powder takes sounded worse). Strides, landings and crashes use the
  game's sneak footsteps and jump-landings for the ground (crashes: wood for trees and props, stone
  otherwise). Tried and removed: a continuous glide of sneak footsteps (didn't read) and a wind loop (awful).
  Knockdown audio: while the player is ragdolled the engine plays Slow Time's droning, muffled loop
  (VOCShoutFXSlowTimeActiveLPSD, mag_cloak_active_lp.wav; Immersive Sounds Compendium's "Silent Player
  Ragdoll" replaces that file with silence everywhere). We mute it only during our tumbles, by raising its
  static attenuation to 100 dB from just before the knockdown until the tumble's over (and on load). Tested.
- Settings menu in pages: General, Controls, Speed, Body, Camera, Sound, Stamina, Impacts.

Controller is the intended way to play; keyboard and mouse work but are rougher. True Directional Movement is a
soft requirement (see Later).

## Next: release

- [x] Release 0.1.0 (Skyrim's 15th anniversary, Nov 11 2026): last in-game test of this build, push, then
      `build.py source` and upload the mod and the source zip to Nexus. The page text is nexus/description.txt.
- [x] Licenses in the Nexus download: the build copies dist/ (README, CHANGELOG, COMPATIBILITY,
      THIRD-PARTY-NOTICES, licenses/) and our LICENSE into build/mod/SKSE/Plugins/SkiingInSkyrim/ (nothing at
      the Data root, where other mods' LICENSE.txt files would collide), plus commitlink.txt pointing at the
      commit it was built from. `build.py source` zips the committed source for the Nexus optional file (GPL
      Corresponding Source). Keep CHANGELOG.md's section for each VERSION up to date.

## Later / ideas

- [ ] Ski poles, then pole plants at turn transitions and while skating.
- [ ] Landing absorption: the legs soak up the landing. Optional; powder already hides most small landings.
- [ ] Surfaces: skis work best on snow but never stop working. Extra drag per material on top of the usual drag,
      so steepness still wins: snow as now; ice faster with weaker edges; grass and mud slower; dirt, sand and
      gravel slower still; stone and wood (roads, towns) heavy, mostly pushing on the flat. Unknown material counts
      as snow; blend changes over a short distance; keep the last surface in the air. One setting, "non-snow
      friction" (0 = surfaces ignored). Later: own scrape sounds for stone and ice, skating on grass costs more,
      Seasons readings. The powder probe (plugin/Surface) already reads the land material and texture at the
      feet, the character controller's surfaceMaterial (what footsteps use, rocks included) and whether you're
      on the landscape or an object; the surface: log lines show them. The landscape's collision is mostly on
      the ground layer, only now and then terrain. Don't look materials up by shape key: that crashed inside
      compressed meshes.
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

## Dropped

- Powder planing: the skis and hips rising out of deformed snow (Deformable Terrain, Community Shaders' Snow
  Deformation) with speed. Tried and removed: high lift looked like water skiing, low lift still left no tracks
  (the deformation mods don't register skis held above the ground) and looked little different, and the depth
  didn't follow the same road and path rules as powder, so the skis floated on packed paths.

## Last

- [ ] Ski-run quests: start at marker A, pass markers B, C, ... in order; succeed at the last marker, fail if the
      skis are taken off (Papyrus OnObjectUnequipped / item removed) before reaching the next one.
