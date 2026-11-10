# Compatibility

The settings menu's General page has a Compatibility section showing which of these mods were found and what
they're doing.

## Game versions

Built and tested on Steam Skyrim 1.6.640. It's built on CommonLibSSE-NG and Address Library, so it should work on
any version they support: Steam 1.5.97 through 1.7.104, and GOG 1.6.659 and 1.6.1179, with the matching SKSE and
Address Library (GOG needs Address Library's GOG databases, which the all-in-one download includes).
If you're on another version, give it a try and report back. Skyrim VR is not supported: the plugin refuses to load
there.

It adds no scripts and nothing to your save, so you can install or uninstall it at any time. Uninstalling only
removes the skis from your inventory.

## True Directional Movement

Recommended. While you ski, Skiing in Skyrim takes control of your character's facing through TDM's API, so the
skis point where they're going rather than where you're running. TDM gets it back when you take the skis off.
Without TDM, third person is untested and turning may feel tied to the camera.

## First person

Not supported yet: turning doesn't work in first person. Ski in third person.

## SmoothCam

Works. The chase camera steers the third-person view along your direction of travel, and SmoothCam smooths it. If
the camera stutters or fights itself, turn one of them off.

## Enhanced Death Cam

When you tumble, Skiing in Skyrim films the fall itself in the game's knockdown camera.

- With SmoothCam installed, Enhanced Death Cam keeps the game's knockdown camera, and the tumble camera works.
- Without SmoothCam, its ragdoll camera keeps third person during a knockdown, so there's no tumble camera. To use
  ours, set `bEnable = false` under `[Ragdoll Camera]` in `po3_EnhancedDeathCamera.ini`.

## Ice is Slick

Works together. Both mods change how you move; Skiing in Skyrim's movement is applied last while you ski. The menu
shows whether anything runs after it.

## Snow deformation (Deformable Terrain, Community Shaders' Snow Deformation)

Not needed. Powder is detected from the landscape itself, so it's the same with or without them.

## Revive mods (PlayerSelfRevive and similar)

Skiing stops while you're downed and the skis stay on. If a tumble leaves you downed, the revive mod's camera takes
over once you stop rolling.

## Ragdolls

Tumbles use the game's own ragdoll, so ragdoll and skeleton mods change how a tumble looks. Physics above 60 FPS
can make ragdolls flail harder.

## HDT-SMP

Safe: the ski stance leaves the physics bones to the game.
