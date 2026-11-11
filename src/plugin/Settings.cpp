#include "plugin/Settings.h"

#include "core/Ini.h"

#include <fstream>
#include <sstream>

namespace SkiingInSkyrim::Settings
{
    namespace
    {
        constexpr auto kPath = "Data/SKSE/Plugins/SkiingInSkyrim.ini";

        constexpr std::uint32_t kLeftShift = 0x2A;
        constexpr std::uint32_t kGamepadX = 0x4000;

        using S = Core::SkiSettings;
        constexpr FloatField kSteeringFields[] = {
            { "SteerDegreesPerSecond", "Steering rate", "Degrees per second the skis turn toward where the stick (or "
                                                        "W/A/S/D) aims, at a full push.",
                &S::steerDegreesPerSecond, 10.0F, 360.0F, "%.0f" },
            { "AimDeadzone", "Stick deadzone", "Fraction of the stick's travel, from the centre, that does nothing.",
                &S::aimDeadzone, 0.0F, 0.6F, "%.2f" },
            { "TriggerEdgeRamp", "Trigger edge ramp", "Full edge range per second when edging with the triggers; "
                                                      "smooths the trigger.",
                &S::triggerEdgeRamp, 1.0F, 30.0F, "%.1f" },
            { "MouseEdgeRamp", "Mouse edge ramp", "Full edge range per second while a mouse button is held.",
                &S::buttonEdgeRamp, 0.2F, 10.0F, "%.2f" },
            { "MouseEdgeRelease", "Mouse edge release", "Full edge range per second after a mouse button is released; "
                                                        "slow keeps the edge from a tap.",
                &S::buttonEdgeRelease, 0.1F, 10.0F, "%.2f" },
        };

        constexpr FloatField kSpeedFields[] = {
            { "Gravity", "Gravity", "m/s^2. Scales how hard slopes pull you.", &S::gravity, 2.0F, 20.0F, "%.2f" },
            { "SnowFriction", "Snow friction", "Friction coefficient. Slopes gentler than atan(this) won't start a slide.",
                &S::snowFriction, 0.0F, 0.3F, "%.3f" },
            { "DragPerSpeedSquared", "Air drag", "Deceleration per (m/s)^2. Lower means faster everywhere, most of "
                                                 "all on gentle slopes.",
                &S::dragPerSpeedSquared, 0.0F, 0.05F, "%.4f" },
            { "DragPerSpeedFourth", "Speed limiter", "Deceleration per (m/s)^4: next to nothing at cruising speeds, "
                                                     "steep at high ones. Lower means faster on steep runs.",
                &S::dragPerSpeedFourth, 0.0F, 0.0003F, "%.6f" },
            { "MaxSpeed", "Max speed", "m/s. Hard cap.", &S::maxSpeed, 5.0F, 60.0F, "%.1f" },
            { "TuckDragReduction", "Tuck", "Fraction of air drag cut by tucking (pushing firmly the way the skis "
                                           "point). Higher means faster in a tuck; 0 is off.",
                &S::tuckDragReduction, 0.0F, 0.8F, "%.2f" },
            { "CarvePump", "Carve pump", "A clean carve pushes you forward by this times the turn's sideways "
                                         "acceleration, so holding a carve through a turn keeps or builds speed. 0 is "
                                         "off.",
                &S::carvePump, 0.0F, 0.3F, "%.2f" },
            { "LandingSpeedKept", "Landing speed kept", "Landing on a slope going downhill turns the fall into speed "
                                                        "down it: this fraction of the speed it would add. 1 is all of "
                                                        "it, 0 lands at the speed you flew at.",
                &S::landingSpeedKept, 0.0F, 1.0F, "%.2f" },
        };

        constexpr FloatField kEdgesFields[] = {
            { "SidecutRadius", "Sidecut radius", "Metres. A carving ski follows an arc of sidecut * cos(edge angle); "
                                                 "smaller carves tighter.",
                &S::sidecutRadius, 5.0F, 40.0F, "%.1f" },
            { "MaxEdgeDegrees", "Max edge angle", "Degrees of edge at a full trigger or a fully held mouse button.",
                &S::maxEdgeDegrees, 10.0F, 70.0F, "%.0f" },
            { "EdgeEngageDegrees", "Edge engage angle", "Degrees. Below this the ski is only partly engaged and "
                                                        "carves a wider arc.",
                &S::edgeEngageDegrees, 0.0F, 30.0F, "%.0f" },
            { "FlatGrip", "Flat grip", "Sideways friction of flat skis: how fast a smear or side-slip slows you.",
                &S::flatGrip, 0.0F, 0.5F, "%.3f" },
            { "EdgeGripFactor", "Edge grip", "Sideways grip added by edging, times tan(edge angle). Limits how tight you can "
                                             "carve at speed and how hard a hockey stop brakes.",
                &S::edgeGrip, 0.1F, 2.0F, "%.2f" },
            { "SelfAlignDegreesPerSecond", "Self-align rate", "Degrees per second flat skis smearing sideways at speed "
                                                              "swing round to where you're going, with the stick centred "
                                                              "and nothing held. Slow side-slips are left alone; 0 is off.",
                &S::selfAlignDegreesPerSecond, 0.0F, 180.0F, "%.0f" },
        };

        constexpr FloatField kSkidFields[] = {
            { "PivotMaxDegrees", "Skid twist", "Degrees the skis twist (pivot) when the skid button is held fully.",
                &S::pivotMaxDegrees, 20.0F, 120.0F, "%.0f" },
            { "PivotBuildSeconds", "Skid twist time", "Seconds of holding skid to reach the full twist; a shorter hold "
                                                       "gives part of it.",
                &S::pivotBuildSeconds, 0.05F, 2.0F, "%.2f" },
            { "PivotReturnSeconds", "Skid return time", "Seconds for the skis to untwist after releasing skid.",
                &S::pivotReturnSeconds, 0.05F, 2.0F, "%.2f" },
            { "ParkSpeed", "Stop speed", "m/s. Slowing below this while holding skid counts as stopped: you're held "
                                         "there, and the skis stay where they are when you let go.",
                &S::parkSpeed, 0.0F, 2.0F, "%.2f" },
        };

        constexpr FloatField kSkatingFields[] = {
            { "SkateImpulse", "Skate stride", "m/s each skating stride adds from a standstill (on gentle ground, stick "
                                              "pushed forward, squeeze the triggers alternately). 0 is off.",
                &S::skateImpulse, 0.0F, 3.0F, "%.2f" },
            { "SkatePushSeconds", "Skate push time", "Seconds each stride's push lasts, as the leg pushes out.",
                &S::skatePushSeconds, 0.1F, 1.0F, "%.2f" },
            { "SkateMaxSpeed", "Skate top speed", "m/s. Strides push less the closer you are to this, and not at all "
                                                  "beyond it.",
                &S::skateMaxSpeed, 1.0F, 12.0F, "%.1f" },
            { "SkateMaxSlopeDegrees", "Skate max slope", "Degrees. Steeper than this, the triggers only edge.",
                &S::skateMaxSlopeDegrees, 0.0F, 25.0F, "%.0f" },
        };

        constexpr FloatField kPowderFields[] = {
            { "PowderDragPerSpeedFourth", "Powder drag", "Deceleration per (m/s)^4 from ploughing through deep snow, "
                                                         "when powder is on, on top of drag. It grows steeply with "
                                                         "speed, so it bites on fast steep runs and barely touches "
                                                         "gentle cruising.",
                &S::powderDragPerSpeedFourth, 0.0F, 0.0003F, "%.6f" },
            { "PowderSideGrip", "Powder side grip", "Flat grip is multiplied by this in powder: skids, stops and "
                                                    "steered turns bite harder.",
                &S::powderSideGrip, 1.0F, 4.0F, "%.2f" },
        };
        constexpr BoolField kPowderSwitches[] = {
            { "Powder", "Powder", "Deep snow is slower, skids and stops bite harder, landings are softer (see the "
                                  "Powder sliders).",
                &S::powder },
            { "AutoPowder", "Detect snow", "Powder is as deep as the ground's snow: full off roads, paths and trails "
                                           "where it's snow all around, none on rock, grass or objects, blending "
                                           "between over a few metres. Off: powder everywhere.",
                &S::autoPowder },
        };

        constexpr FloatField kAdvancedFields[] = {
            { "GroundHugSeconds", "Ground hug time", "Seconds off the ground over a crest (not a jump) during which "
                                                     "you're held to the slope; longer absorbs bigger crests, 0 is off.",
                &S::groundHugSeconds, 0.0F, 1.0F, "%.2f" },
            { "GroundHugGravity", "Ground hug pull", "m/s^2 of extra downward pull while held to the slope.",
                &S::groundHugGravity, 0.0F, 60.0F, "%.0f" },
            { "GroundHugMaxDropSpeed", "Ground hug max drop", "m/s. The pull can't take you down faster than this "
                                                              "beyond following the slope, so you don't slam back onto it.",
                &S::groundHugMaxDropSpeed, 0.0F, 10.0F, "%.1f" },
        };
        constexpr BoolField kAdvancedSwitches[] = {
            { "SmoothHeight", "Smooth steep slopes", "Keep the character at the physics body's height every frame, "
                                                     "so fast descents of steep slopes don't jitter up and down.",
                &S::smoothHeight },
        };

        constexpr FloatField kBlockedFields[] = {
            { "BlockedRatio", "Counts as blocked below", "If you're moving slower than this share of the speed the skis "
                                                         "should have (0.75 is 75%), something is in your way: a wall, "
                                                         "fence, person or rock.",
                &S::blockedRatio, 0.3F, 0.95F, "%.2f" },
            { "BlockedBlend", "Give way rate", "While blocked, how quickly your skiing speed drops to what you're "
                                               "actually managing, per frame. Higher stops you dead at an obstacle; "
                                               "lower holds on to speed and may shoot you off when you slide clear.",
                &S::blockedBlend, 0.05F, 1.0F, "%.2f" },
            { "BlockedMinSpeed", "Ignore below speed", "m/s. Slower than this, the check is skipped: tiny speeds are "
                                                       "too noisy to judge.",
                &S::blockedMinSpeed, 0.0F, 3.0F, "%.2f" },
        };

        constexpr FloatField kStaminaFields[] = {
            { "PerCarveSecond", "Carving stamina", "Stamina per second holding a full edge while carving; light edges "
                                                   "cost far less. 0 is off.",
                &S::staminaPerCarveSecond, 0.0F, 40.0F, "%.1f" },
            { "PerStride", "Skating stamina", "Stamina per skating stride. Out of stamina, you can't skate. 0 is off.",
                &S::staminaPerStride, 0.0F, 30.0F, "%.1f" },
            { "TiredEdgeFraction", "Out-of-stamina edge", "Out of stamina, edges reach this fraction of full: 0.5 is "
                                                          "half, 1.0 the full edge. Does nothing with both stamina "
                                                          "costs at 0.",
                &S::tiredEdgeFraction, 0.0F, 1.0F, "%.2f" },
        };

        constexpr FloatField kImpactsFields[] = {
            { "LandingDamageScale", "Landing damage", "Damage from landings, times the game's own for a fall as hard "
                                                      "as the speed the air added into the slope (onto flat "
                                                      "ground, the height dropped). 0 is off.",
                &S::landingDamageScale, 0.0F, 3.0F, "%.2f" },
            { "CrashDamageScale", "Crash damage", "Damage from skiing into trees, rocks and walls, times the game's "
                                                  "own for a fall as hard as the speed lost. 0 is off.",
                &S::crashDamageScale, 0.0F, 3.0F, "%.2f" },
            { "CrashMinSurfaceDegrees", "Crash surface", "Degrees. Only surfaces at least this steep count as a crash; "
                                                         "anything gentler is a landing.",
                &S::crashMinSurfaceDegrees, 30.0F, 90.0F, "%.0f" },
            { "LandingTumbleSpeed", "Landing tumble speed", "m/s. Landing at least this fast into the slope (after "
                                                            "the powder's share) makes you tumble. 9.5 m/s is about a 15 ft "
                                                            "drop onto flat ground (25 ft in powder).",
                &S::landingTumbleSpeed, 5.0F, 40.0F, "%.1f" },
            { "CrashTumbleSpeed", "Crash tumble speed", "m/s. Losing at least this much speed into a tree, rock or "
                                                        "wall knocks you down. 6.7 m/s is 15 mph; crashes start to "
                                                        "hurt at about 13 m/s (29 mph).",
                &S::crashTumbleSpeed, 3.0F, 40.0F, "%.1f" },
            { "RagdollThrow", "Tumble throw", "How hard the tumble throws you on the way you were going, per m/s of "
                                              "speed. 0 just knocks you down.",
                &S::ragdollThrow, 0.0F, 1.0F, "%.2f" },
            { "RagdollSpeedKept", "Tumble speed kept", "How much of your speed the body keeps going into the tumble. "
                                                       "1 slides on as fast as you were skiing; 0 drops on the spot.",
                &S::ragdollSpeedKept, 0.0F, 1.0F, "%.2f" },
            { "TumbleSlideSeconds", "Tumble slide", "Seconds the body tumbles freely before it's braked to a stop. "
                                                    "10 lets it roll on down the hill.",
                &S::tumbleSlideSeconds, 0.0F, 10.0F, "%.1f" },
            { "PowderLandingMetres", "Powder landing", "Metres of fall the powder absorbs on landing: smaller drops "
                                                       "are free, bigger ones hurt by the rest.",
                &S::powderLandingMetres, 0.0F, 10.0F, "%.1f" },
        };
        constexpr BoolField kImpactsSwitches[] = {
            { "ImpactDamage", "Impact damage", "Hard landings and crashes into trees, rocks and walls hurt, by how "
                                               "much speed they take away (see the damage sliders). The game's own "
                                               "fall damage is always off on skis.",
                &S::impactDamage },
            { "CrashRagdoll", "Tumble on big hits", "A landing or crash at least as hard as its tumble speed throws "
                                                    "you into a ragdoll tumble, then you get up.",
                &S::crashRagdoll },
            { "HurtTumbleRemovesSkis", "Tumbling with damage takes the skis off",
                "When a hit that hurts you makes you tumble, the skis come off (they stay in your inventory); put "
                "them back on to ski again.",
                &S::hurtTumbleRemovesSkis },
            { "HarmlessTumbleRemovesSkis", "Tumbling without damage takes the skis off",
                "When a hit that doesn't hurt you makes you tumble, the skis come off too. Off: you get up still "
                "wearing them.",
                &S::harmlessTumbleRemovesSkis },
        };

        constexpr FloatField kFlowFields[] = {
            { "FillMetres", "Height to fill", "Metres to descend on skis, without a tumble, to fill the bar. 150 m "
                                              "is a good run down a mountainside.",
                &S::flowFillMetres, 20.0F, 1000.0F, "%.0f" },
            { "ReserveMetres", "Full bar takes", "Metres of fall a full bar takes from hard landings and crashes "
                                                 "before it's empty. A flat landing from 15 m takes 15; the last hit "
                                                 "is let off however big.",
                &S::flowReserveMetres, 5.0F, 100.0F, "%.0f" },
        };
        constexpr BoolField kFlowSwitches[] = {
            { "Enabled", "Flow", "Skiing down without falling fills a bar in the top right corner (white). Full "
                                 "(gold), hard landings and crashes are taken from it instead of hurting or "
                                 "knocking you down, until it's empty. A tumble while it fills starts it over. "
                                 "Not kept in your save.",
                &S::flow },
        };

        constexpr PhysicsGroup kPhysicsGroups[] = {
            { Page::General, "Flow", "Flow", kFlowSwitches, kFlowFields,
                "For big airs: descend without falling to fill the bar, then spend it on landings and crashes "
                "that would otherwise knock you down." },
            { Page::Controls, "Steering", "Steering", {}, kSteeringFields },
            { Page::Speed, "Speed", "Speed", {}, kSpeedFields },
            { Page::Speed, "Edges", "Edges and turns", {}, kEdgesFields },
            { Page::Speed, "Skid", "Skid", {}, kSkidFields },
            { Page::Speed, "Skating", "Skating", {}, kSkatingFields },
            { Page::Speed, "Powder", "Powder", kPowderSwitches, kPowderFields },
            { Page::Speed, "BlockedPath", "Blocked path detection and handling", {}, kBlockedFields,
                "When something stops you, the skis give up the speed they can't use instead of saving it for later. "
                "Works whether or not impact damage is on." },
            { Page::Speed, "Advanced", "Advanced", kAdvancedSwitches, kAdvancedFields },
            { Page::Stamina, "Stamina", "", {}, kStaminaFields },
            { Page::Impacts, "Impacts", "", kImpactsSwitches, kImpactsFields },
        };

        using C = Core::CameraFollowSettings;
        constexpr CameraField kCameraFields[] = {
            { "FollowSeconds", "Camera follow lag", "Seconds. How lazily the camera swings round to your direction of "
                                                    "travel once it's outside the lazy zone.",
                &C::followSeconds, 0.05F, 2.0F, "%.2f" },
            { "TravelSmoothingSeconds", "Travel smoothing", "Seconds the direction of travel is averaged over first, so "
                                                            "quick back-and-forth carves cancel out.",
                &C::travelSmoothingSeconds, 0.0F, 2.0F, "%.2f" },
            { "LazyZoneDegrees", "Lazy zone", "Degrees. Within this of the camera, the direction of travel only pulls "
                                              "it round slowly.",
                &C::lazyZoneDegrees, 0.0F, 45.0F, "%.0f" },
            { "RecenterSeconds", "Lazy zone recenter", "Seconds. How slowly the camera recenters within the lazy zone.",
                &C::recenterSeconds, 0.2F, 6.0F, "%.2f" },
            { "LookHoldSeconds", "Look-around hold", "Seconds the camera stays where you looked before easing back.",
                &C::lookHoldSeconds, 0.0F, 5.0F, "%.2f" },
            { "LookReturnSeconds", "Look-around return", "Seconds. How lazily the camera eases back after looking around.",
                &C::lookReturnSeconds, 0.05F, 3.0F, "%.2f" },
            { "TumbleFreezeSeconds", "Tumble camera freeze", "Seconds the camera stays exactly where it was when "
                                                             "you tumble, before it follows the body.",
                &C::tumbleFreezeSeconds, 0.0F, 3.0F, "%.2f" },
            { "TumbleFollowSeconds", "Tumble camera follow", "Seconds. How lazily the camera drifts after the "
                                                             "tumbling body; it never turns with it.",
                &C::tumbleFollowSeconds, 0.2F, 5.0F, "%.2f" },
            { "TumbleHeightMetres", "Tumble camera height", "Metres. The tumble camera stays at least this high "
                                                            "above the ground.",
                &C::tumbleHeightMetres, 0.3F, 5.0F, "%.1f" },
        };

        using A = Core::SkiSoundSettings;
        constexpr SoundSwitch kSoundSwitches[] = {
            { "Enabled", "Ski sounds", "Turns, skids, skating strides, landings and crashes. Running straight is "
                                       "quiet, as in good snow.",
                &A::enabled },
            { "SilenceFootsteps", "Silence footsteps", "The walking animation under the ski stance plays footsteps; "
                                                       "keep them quiet while skiing.",
                &A::silenceFootsteps },
        };
        constexpr SoundField kSoundFields[] = {
            { "Volume", "Volume", "All the ski sounds, on top of the game's own volume sliders.", &A::volume, 0.0F,
                1.0F, "%.2f" },
            { "TurnVolume", "Turns", "A sound as each turn starts and at each change of direction, louder the faster "
                                     "and harder.",
                &A::turnVolume, 0.0F, 1.0F, "%.2f" },
            { "SkidVolume", "Skids", "The skis sliding sideways: skids and hockey stops, louder the faster.",
                &A::skidVolume, 0.0F, 1.0F, "%.2f" },
            { "StrideVolume", "Skating strides", "Each skating stride's push.", &A::strideVolume, 0.0F, 1.0F, "%.2f" },
            { "LandingVolume", "Landings and crashes", "Touching down after air, and skiing into trees, rocks and "
                                                       "walls; louder the harder.",
                &A::landingVolume, 0.0F, 1.0F, "%.2f" },
            { "TurnMinSpeed", "Turns from", "m/s. Slower turns are silent.", &A::turnMinSpeed, 0.0F, 15.0F, "%.1f" },
            { "TurnFullSpeed", "Turns full at", "m/s. Turns are at full volume from this speed.", &A::turnFullSpeed,
                2.0F, 40.0F, "%.1f" },
            { "TurnAcceleration", "Counts as a turn", "m/s^2 the path curves at. Lower makes gentler turns sound too.",
                &A::turnAcceleration, 0.5F, 15.0F, "%.1f" },
            { "SkidStartSpeed", "Skids from", "m/s sideways. Slower side-slips are silent.", &A::skidStartSpeed, 0.2F,
                5.0F, "%.1f" },
            { "SkidFullSpeed", "Skids full at", "m/s sideways. Skids are at full volume from this speed.",
                &A::skidFullSpeed, 1.0F, 20.0F, "%.1f" },
            { "SkidRepeatSeconds", "Long skid repeat", "Seconds. A skid that keeps going plays another sound this "
                                                       "often (varied a little).",
                &A::skidRepeatSeconds, 0.2F, 3.0F, "%.2f" },
            { "PitchVariation", "Pitch variation", "Each sound's pitch is moved up or down by up to this fraction, "
                                                   "so repeats don't sound the same.",
                &A::pitchVariation, 0.0F, 0.3F, "%.2f" },
            { "VolumeVariation", "Volume variation", "Each sound is up to this fraction quieter, at random.",
                &A::volumeVariation, 0.0F, 0.8F, "%.2f" },
        };

        using T = Core::StanceSettings;
        constexpr StanceField kStanceFields[] = {
            { "AnkleFlexDegrees", "Ankle flex", "Degrees the shins lean forward.", &T::ankleFlexDegrees, 0.0F, 45.0F, "%.0f" },
            { "HipFlexDegrees", "Hip flex", "Degrees the thighs lean forward; with the ankles, how far the knees bend.",
                &T::hipFlexDegrees, 0.0F, 90.0F, "%.0f" },
            { "TorsoLeanDegrees", "Torso lean", "Degrees the upper body leans forward.", &T::torsoLeanDegrees, -10.0F, 80.0F,
                "%.0f" },
            { "ArmOutDegrees", "Arms out", "Degrees the arms are held out from the sides.", &T::armOutDegrees, 0.0F, 90.0F,
                "%.0f" },
            { "ArmForwardDegrees", "Arms forward", "Degrees the arms swing forward from hanging, relative to the torso.",
                &T::armForwardDegrees, -30.0F, 120.0F, "%.0f" },
            { "ElbowBendDegrees", "Elbow bend", "Degrees the elbows bend, bringing the hands forward.", &T::elbowBendDegrees,
                0.0F, 140.0F, "%.0f" },
            { "TuckAnkleDegrees", "Tuck: ankles", "Degrees of ankle flex added at a full tuck.", &T::tuckAnkleDegrees, 0.0F,
                40.0F, "%.0f" },
            { "TuckHipDegrees", "Tuck: hips", "Degrees of hip flex added at a full tuck.", &T::tuckHipDegrees, 0.0F, 80.0F,
                "%.0f" },
            { "TuckLeanDegrees", "Tuck: torso", "Degrees of torso lean added at a full tuck.", &T::tuckLeanDegrees, 0.0F, 80.0F,
                "%.0f" },
            { "TuckArmForwardDegrees", "Tuck: arms", "Degrees the arms swing further forward at a full tuck.",
                &T::tuckArmForwardDegrees, -30.0F, 90.0F, "%.0f" },
            { "TurnLean", "Turn: lean", "How far the body leans into a turn, as a fraction of what the turn's force "
                                        "calls for. 0 stays upright.",
                &T::turnLean, 0.0F, 2.0F, "%.2f" },
            { "HipAngulation", "Turn: hips in", "Fraction of the edge angle the lean doesn't cover that the hips "
                                                "push in by, so edging across the hill puts the hips into it.",
                &T::hipAngulation, 0.0F, 1.0F, "%.2f" },
            { "UpperBodyUpright", "Turn: upper body upright", "Fraction of the lean the upper body takes back, "
                                                             "staying more upright than the legs.",
                &T::upperBodyUpright, 0.0F, 1.0F, "%.2f" },
            { "SkateSplayDegrees", "Skate: V", "Degrees each ski's tip points out while skating.", &T::skateSplayDegrees,
                0.0F, 40.0F, "%.0f" },
            { "SkatePushOut", "Skate: push out", "How far (game units) each stride pushes the ski out to the side.",
                &T::skatePushOut, 0.0F, 60.0F, "%.0f" },
            { "SkateLift", "Skate: lift", "How high (game units) the pushing ski lifts as it comes back.", &T::skateLift,
                0.0F, 20.0F, "%.0f" },
            { "SkateEdgeDegrees", "Skate: edge", "Degrees the pushing ski rolls onto its inside edge.", &T::skateEdgeDegrees,
                0.0F, 60.0F, "%.0f" },
            { "SkateArmSwingDegrees", "Skate: arm swing", "Degrees the arms swing forward and back with the strides.",
                &T::skateArmSwingDegrees, 0.0F, 90.0F, "%.0f" },
            { "SkateLeanDegrees", "Skate: torso", "Degrees of torso lean added while skating.", &T::skateLeanDegrees, 0.0F,
                40.0F, "%.0f" },
            { "SkidTwist", "Skid: upper body turn", "Skidding, the upper body turns back toward where you're going by "
                                                    "this fraction of the angle the skis slip at, while the legs stay "
                                                    "with the skis.",
                &T::skidTwist, 0.0F, 1.0F, "%.2f" },
            { "SkidTwistMaxDegrees", "Skid: max turn", "Degrees. The upper body turns no further than this from the skis.",
                &T::skidTwistMaxDegrees, 0.0F, 90.0F, "%.0f" },
            { "FallAnimationDropMetres", "Air: fall animation after", "Metres. Off the ground, once you've dropped this "
                                                                     "far below the highest point, the game's own "
                                                                     "falling animation (or OAR's) takes over. 0 keeps "
                                                                     "the ski stance in the air.",
                &T::fallAnimationDropMetres, 0.0F, 20.0F, "%.1f" },
        };

        constexpr StanceSwitch kStanceSwitches[] = {
            { "SkidStance", "Skid stance", "Skidding, the upper body faces where you're going while the skis turn across "
                                           "it (see the Skid sliders).",
                &T::skidStance },
        };

        struct BindingInfo
        {
            Binding     binding;
            const char* key;  // INI key prefix
            const char* help;
            Button      defaults;
        };

        const BindingInfo kBindings[] = {
            { Binding::SkidKeyboard, "SkidKey", "Skid (keyboard/mouse)", { RE::INPUT_DEVICE::kKeyboard, kLeftShift } },
            { Binding::SkidGamepad, "SkidButton", "Skid (gamepad)", { RE::INPUT_DEVICE::kGamepad, kGamepadX } },
        };
        constexpr std::size_t kBindingCount = std::size(kBindings);

        bool Accepts(Binding binding, RE::INPUT_DEVICE device)
        {
            switch (binding) {
            case Binding::SkidKeyboard:
                return device == RE::INPUT_DEVICE::kKeyboard || device == RE::INPUT_DEVICE::kMouse;
            case Binding::SkidGamepad:
                return device == RE::INPUT_DEVICE::kGamepad;
            }
            return false;
        }

        struct State
        {
            Core::SkiSettings                       physics;
            Core::CameraFollowSettings              camera;
            std::array<Button, kBindingCount>       bindings;
            std::array<std::string, kBindingCount>  bindingNames;
            bool                                    modEnabled{ true };
            bool                                    showOverlay{ false };
            bool                                    lookSteersKeyboardMouse{ true };
            bool                                    lookSteersGamepad{ false };
            bool                                    traceShaking{ false };
            bool                                    detailedLog{ false };
            bool                                    stanceEnabled{ true };
            Core::StanceSettings                    stance;
            Core::SkiSoundSettings                  sound;
        };

        std::mutex g_lock;
        State      g_state;

        // The game names gamepad buttons by their XInput bit (16384 for X); these are the Xbox labels, which
        // the Steam Deck shares. The triggers are Skyrim's own codes.
        constexpr std::pair<std::uint32_t, const char*> kGamepadNames[] = {
            { 0x0001, "D-pad Up" }, { 0x0002, "D-pad Down" }, { 0x0004, "D-pad Left" }, { 0x0008, "D-pad Right" },
            { 0x0010, "Start" }, { 0x0020, "Back" }, { 0x0040, "Left Stick (click)" }, { 0x0080, "Right Stick (click)" },
            { 0x0100, "LB" }, { 0x0200, "RB" }, { 0x1000, "A" }, { 0x2000, "B" }, { 0x4000, "X" }, { 0x8000, "Y" },
            { 0x0009, "LT" }, { 0x000A, "RT" },
        };

        // Lowercase letters and digits only, so "d-pad up" and "DpadUp" both match "D-pad Up".
        std::string Simplified(std::string_view text)
        {
            std::string result;
            for (const char c : text) {
                if (std::isalnum(static_cast<unsigned char>(c))) {
                    result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
            }
            return result;
        }

        const char* GamepadName(std::uint32_t id)
        {
            for (const auto& [gamepadId, label] : kGamepadNames) {
                if (gamepadId == id) {
                    return label;
                }
            }
            return nullptr;
        }

        std::optional<std::uint32_t> GamepadId(std::string_view name)
        {
            const auto wanted = Simplified(name);
            for (const auto& [id, label] : kGamepadNames) {
                if (Simplified(label) == wanted) {
                    return id;
                }
            }
            return std::nullopt;
        }

        std::string ButtonName(Button button)
        {
            if (const char* label = button.device == RE::INPUT_DEVICE::kGamepad ? GamepadName(button.id) : nullptr) {
                return label;
            }
            RE::BSFixedString name;
            auto* manager = RE::BSInputDeviceManager::GetSingleton();
            if (manager && manager->GetButtonNameFromID(button.device, static_cast<std::int32_t>(button.id), name) &&
                !name.empty()) {
                return name.c_str();
            }
            return fmt::format("device {} button {}", static_cast<int>(button.device), button.id);
        }

        std::string BoundName(Button button) { return button.Bound() ? ButtonName(button) : std::string{}; }

        // Gamepad: "SkidButton = X" (or none). Keyboard or mouse: a device line and the key's number.
        std::optional<Button> ReadButton(const Core::Ini& ini, const BindingInfo& info)
        {
            const std::string key = info.key;
            if (info.binding == Binding::SkidGamepad) {
                const auto value = ini.Get("Controls", key);
                if (!value) {
                    return std::nullopt;
                }
                if (Simplified(*value).empty() || Simplified(*value) == "none") {
                    return Button{};
                }
                const auto id = GamepadId(*value);
                return id ? std::optional{ Button{ RE::INPUT_DEVICE::kGamepad, *id } } : std::nullopt;
            }
            const auto device = ini.GetInt("Controls", key + "Device");
            const auto id = ini.GetInt("Controls", key);
            if (!device) {
                return std::nullopt;
            }
            if (*device < 0) {
                return Button{};
            }
            if (!id || *id < 0 || !Accepts(info.binding, static_cast<RE::INPUT_DEVICE>(*device))) {
                return std::nullopt;
            }
            return Button{ static_cast<RE::INPUT_DEVICE>(*device), static_cast<std::uint32_t>(*id) };
        }

        void WriteButton(std::string& text, const BindingInfo& info, Button button)
        {
            if (info.binding == Binding::SkidGamepad) {
                const char* name = button.Bound() ? GamepadName(button.id) : nullptr;
                text += fmt::format("{} = {}\n", info.key, name ? name : "none");
                return;
            }
            text += fmt::format("{}Device = {}\n{} = {}\n", info.key,
                button.Bound() ? static_cast<int>(button.device) : -1, info.key, button.id);
        }
    }

    std::span<const PhysicsGroup> PhysicsGroups() noexcept { return kPhysicsGroups; }
    std::span<const CameraField> CameraFields() noexcept { return kCameraFields; }
    std::span<const StanceField> StanceFields() noexcept { return kStanceFields; }
    std::span<const StanceSwitch> StanceSwitches() noexcept { return kStanceSwitches; }
    std::span<const SoundField> SoundFields() noexcept { return kSoundFields; }
    std::span<const SoundSwitch> SoundSwitches() noexcept { return kSoundSwitches; }

    void Load()
    {
        State loaded;
        for (std::size_t i = 0; i < kBindingCount; ++i) {
            loaded.bindings[i] = kBindings[i].defaults;
        }

        std::ifstream file(kPath, std::ios::binary);
        if (file) {
            std::stringstream text;
            text << file.rdbuf();
            const auto ini = Core::Ini::Parse(text.str());

            if (const auto value = ini.GetInt("General", "ModEnabled")) {
                loaded.modEnabled = *value != 0;
            }
            for (std::size_t i = 0; i < kBindingCount; ++i) {
                if (const auto button = ReadButton(ini, kBindings[i])) {
                    loaded.bindings[i] = *button;
                }
            }
            if (const auto value = ini.GetInt("Controls", "LookSteersKeyboardMouse")) {
                loaded.lookSteersKeyboardMouse = *value != 0;
            }
            if (const auto value = ini.GetInt("Controls", "LookSteersGamepad")) {
                loaded.lookSteersGamepad = *value != 0;
            }
            for (const auto& group : kPhysicsGroups) {
                for (const auto& field : group.switches) {
                    if (const auto value = ini.GetInt(group.section, field.key)) {
                        loaded.physics.*field.member = *value != 0;
                    }
                }
                for (const auto& field : group.fields) {
                    if (const auto value = ini.GetFloat(group.section, field.key)) {
                        loaded.physics.*field.member = std::clamp(*value, field.min, field.max);
                    }
                }
            }
            if (const auto value = ini.GetInt("Camera", "FollowTravel")) {
                loaded.camera.enabled = *value != 0;
            }
            for (const auto& field : kCameraFields) {
                if (const auto value = ini.GetFloat("Camera", field.key)) {
                    loaded.camera.*field.member = std::clamp(*value, field.min, field.max);
                }
            }
            if (const auto value = ini.GetInt("Display", "ShowOverlay")) {
                loaded.showOverlay = *value != 0;
            }
            if (const auto value = ini.GetInt("Debug", "TraceShaking")) {
                loaded.traceShaking = *value != 0;
            }
            if (const auto value = ini.GetInt("Debug", "DetailedLog")) {
                loaded.detailedLog = *value != 0;
            }
            if (const auto value = ini.GetInt("Stance", "Enabled")) {
                loaded.stanceEnabled = *value != 0;
            }
            for (const auto& field : kStanceFields) {
                if (const auto value = ini.GetFloat("Stance", field.key)) {
                    loaded.stance.*field.member = std::clamp(*value, field.min, field.max);
                }
            }
            for (const auto& field : kStanceSwitches) {
                if (const auto value = ini.GetInt("Stance", field.key)) {
                    loaded.stance.*field.member = *value != 0;
                }
            }
            for (const auto& field : kSoundSwitches) {
                if (const auto value = ini.GetInt("Sound", field.key)) {
                    loaded.sound.*field.member = *value != 0;
                }
            }
            for (const auto& field : kSoundFields) {
                if (const auto value = ini.GetFloat("Sound", field.key)) {
                    loaded.sound.*field.member = std::clamp(*value, field.min, field.max);
                }
            }
            spdlog::info("settings loaded from {}", kPath);
        } else {
            spdlog::info("no {}, using defaults", kPath);
        }
        for (std::size_t i = 0; i < kBindingCount; ++i) {
            loaded.bindingNames[i] = BoundName(loaded.bindings[i]);
        }

        std::scoped_lock lock(g_lock);
        g_state = std::move(loaded);
    }

    void Save()
    {
        State state;
        {
            std::scoped_lock lock(g_lock);
            state = g_state;
        }
        std::string text;
        text += "; Skiing in Skyrim settings. Written by the in-game menu; edit there or here.\n\n";
        text += "[General]\n";
        text += "; Wearing skis turns skiing on (1 on, 0 off: the mod does nothing).\n";
        text += fmt::format("ModEnabled = {}\n\n", state.modEnabled ? 1 : 0);
        text += "[Controls]\n";
        text += "; Keyboard/mouse: device (0 keyboard, 1 mouse, -1 unbound) and key number. Gamepad: a button name\n";
        text += "; (A, B, X, Y, LB, RB, LT, RT, Start, Back, Left Stick (click), Right Stick (click),\n";
        text += "; D-pad Up/Down/Left/Right) or none.\n";
        for (std::size_t i = 0; i < kBindingCount; ++i) {
            const auto& name = state.bindingNames[i];
            text += fmt::format("; {}: {}\n", kBindings[i].help, name.empty() ? "unbound" : name);
            WriteButton(text, kBindings[i], state.bindings[i]);
        }
        text += "; Looking around with the mouse moves where W/A/S/D steer, so the mouse steers too (1 on, 0 off:\n"
                "; they steer relative to the chase camera's own direction).\n";
        text += fmt::format("LookSteersKeyboardMouse = {}\n", state.lookSteersKeyboardMouse ? 1 : 0);
        text += "; Looking around with the right stick moves where the left stick steers (1 on, 0 off).\n";
        text += fmt::format("LookSteersGamepad = {}\n", state.lookSteersGamepad ? 1 : 0);
        text += "\n[Display]\n";
        text += "; Debug overlay with the skis, velocity, fall line and edge angle (1 on, 0 off).\n";
        text += fmt::format("ShowOverlay = {}\n\n", state.showOverlay ? 1 : 0);
        text += "[Debug]\n";
        text += "; Log every frame while the character shakes up and down (1 on, 0 off). Only for chasing jitter.\n";
        text += fmt::format("TraceShaking = {}\n", state.traceShaking ? 1 : 0);
        text += "; Log telemetry, surfaces, airs, every impact and the tumble camera as you ski (1 on, 0 off). For\n"
                "; playtesting and bug reports; it writes several MB an hour.\n";
        text += fmt::format("DetailedLog = {}\n\n", state.detailedLog ? 1 : 0);
        text += "[Stance]\n";
        text += "; Pose the body like a skier while skiing (1 on, 0 off: the game's own animation shows).\n";
        text += fmt::format("Enabled = {}\n", state.stanceEnabled ? 1 : 0);
        for (const auto& field : kStanceSwitches) {
            text += fmt::format("; {} (1 on, 0 off)\n{} = {}\n", field.help, field.key, state.stance.*field.member ? 1 : 0);
        }
        for (const auto& field : kStanceFields) {
            text += fmt::format("; {}\n{} = {}\n", field.help, field.key, state.stance.*field.member);
        }
        text += "\n[Sound]\n";
        for (const auto& field : kSoundSwitches) {
            text += fmt::format("; {} (1 on, 0 off)\n{} = {}\n", field.help, field.key, state.sound.*field.member ? 1 : 0);
        }
        for (const auto& field : kSoundFields) {
            text += fmt::format("; {}\n{} = {}\n", field.help, field.key, state.sound.*field.member);
        }
        text += "\n";
        text += "[Camera]\n";
        text += "; Third-person camera follows your direction of travel while skiing (1 on, 0 off).\n";
        text += fmt::format("FollowTravel = {}\n", state.camera.enabled ? 1 : 0);
        for (const auto& field : kCameraFields) {
            text += fmt::format("; {}\n{} = {}\n", field.help, field.key, state.camera.*field.member);
        }
        for (const auto& group : kPhysicsGroups) {
            text += fmt::format("\n[{}]\n", group.section);
            for (const auto& field : group.switches) {
                text += fmt::format("; {} (1 on, 0 off)\n{} = {}\n", field.help, field.key, state.physics.*field.member ? 1 : 0);
            }
            for (const auto& field : group.fields) {
                text += fmt::format("; {}\n{} = {}\n", field.help, field.key, state.physics.*field.member);
            }
        }

        std::ofstream file(kPath, std::ios::binary | std::ios::trunc);
        if (!file || !(file << text)) {
            spdlog::warn("could not write {}", kPath);
        }
    }

    Core::SkiSettings Physics()
    {
        std::scoped_lock lock(g_lock);
        return g_state.physics;
    }

    void SetPhysics(const Core::SkiSettings& physics)
    {
        std::scoped_lock lock(g_lock);
        g_state.physics = physics;
    }

    Core::CameraFollowSettings Camera()
    {
        std::scoped_lock lock(g_lock);
        return g_state.camera;
    }

    void SetCamera(const Core::CameraFollowSettings& camera)
    {
        std::scoped_lock lock(g_lock);
        g_state.camera = camera;
    }

    Button GetBinding(Binding binding)
    {
        std::scoped_lock lock(g_lock);
        return g_state.bindings[static_cast<std::size_t>(binding)];
    }

    std::string BindingName(Binding binding)
    {
        std::scoped_lock lock(g_lock);
        return g_state.bindingNames[static_cast<std::size_t>(binding)];
    }

    void SetBinding(Binding binding, Button button)
    {
        if (!Accepts(binding, button.device)) {
            button = {};
        }
        auto name = BoundName(button);
        spdlog::info("{} set to {}", kBindings[static_cast<std::size_t>(binding)].help, name.empty() ? "unbound" : name);
        std::scoped_lock lock(g_lock);
        g_state.bindings[static_cast<std::size_t>(binding)] = button;
        g_state.bindingNames[static_cast<std::size_t>(binding)] = std::move(name);
    }

    bool ModEnabled()
    {
        std::scoped_lock lock(g_lock);
        return g_state.modEnabled;
    }

    void SetModEnabled(bool enabled)
    {
        {
            std::scoped_lock lock(g_lock);
            if (g_state.modEnabled == enabled) {
                return;
            }
            g_state.modEnabled = enabled;
        }
        spdlog::info("mod {}", enabled ? "enabled" : "disabled");
    }

    bool ShowOverlay()
    {
        std::scoped_lock lock(g_lock);
        return g_state.showOverlay;
    }

    void SetShowOverlay(bool show)
    {
        std::scoped_lock lock(g_lock);
        g_state.showOverlay = show;
    }

    bool TraceShaking()
    {
        std::scoped_lock lock(g_lock);
        return g_state.traceShaking;
    }

    void SetTraceShaking(bool trace)
    {
        std::scoped_lock lock(g_lock);
        g_state.traceShaking = trace;
    }

    bool DetailedLog()
    {
        std::scoped_lock lock(g_lock);
        return g_state.detailedLog;
    }

    void SetDetailedLog(bool detailed)
    {
        std::scoped_lock lock(g_lock);
        g_state.detailedLog = detailed;
    }

    bool StanceEnabled()
    {
        std::scoped_lock lock(g_lock);
        return g_state.stanceEnabled;
    }

    void SetStanceEnabled(bool enabled)
    {
        std::scoped_lock lock(g_lock);
        g_state.stanceEnabled = enabled;
    }

    Core::StanceSettings Stance()
    {
        std::scoped_lock lock(g_lock);
        return g_state.stance;
    }

    void SetStance(const Core::StanceSettings& stance)
    {
        std::scoped_lock lock(g_lock);
        g_state.stance = stance;
    }

    Core::SkiSoundSettings Sound()
    {
        std::scoped_lock lock(g_lock);
        return g_state.sound;
    }

    void SetSound(const Core::SkiSoundSettings& sound)
    {
        std::scoped_lock lock(g_lock);
        g_state.sound = sound;
    }

    bool LookSteersKeyboardMouse()
    {
        std::scoped_lock lock(g_lock);
        return g_state.lookSteersKeyboardMouse;
    }

    void SetLookSteersKeyboardMouse(bool steers)
    {
        std::scoped_lock lock(g_lock);
        g_state.lookSteersKeyboardMouse = steers;
    }

    bool LookSteersGamepad()
    {
        std::scoped_lock lock(g_lock);
        return g_state.lookSteersGamepad;
    }

    void SetLookSteersGamepad(bool steers)
    {
        std::scoped_lock lock(g_lock);
        g_state.lookSteersGamepad = steers;
    }
}
