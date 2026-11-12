#include "plugin/Skiing.h"

#include "core/Flow.h"
#include "core/Impact.h"
#include "core/Snow.h"
#include "plugin/Camera.h"
#include "plugin/Havok.h"
#include "plugin/Input.h"
#include "plugin/Pose.h"
#include "plugin/Settings.h"
#include "plugin/Skis.h"
#include "plugin/Sounds.h"
#include "plugin/Surface.h"
#include "plugin/Tdm.h"

namespace SkiingInSkyrim::Skiing
{
    namespace
    {
        // ModifyMovementData's vector is in the character's frame (x right, y forward) and measures speed along
        // the ground; in-game it was 1.167x the Havok speed in m/s at walk, run and sprint.
        constexpr float kMovementPerMeterPerSecond = 70.0F / 60.0F;
        constexpr float kUnitsPerMetre = 70.0F;

        // A stick that hasn't reported for this long is ignored.
        constexpr float kStaleStickSeconds = 0.5F;
        constexpr float kStickSmoothingSeconds = 0.08F;
        constexpr float kTriggerDeadzone = 0.02F;
        // Held movement keys push the aim from nothing to full over this long (with the stick's squared
        // response), so a tap nudges and a hold turns; starting again whenever the keys held change.
        constexpr float kKeyAimBuildSeconds = 0.6F;

        struct Command
        {
            bool       active{ false };
            Core::Vec2 velocity{};
        };

        std::mutex g_commandLock;
        Command    g_command;

        Core::GroundTracker g_ground;
        Debug               g_debug;

        std::mutex  g_overlayLock;
        OverlayData g_overlay;

        Body g_body;

        // The path's curve, smoothed over this long so the body leans through a turn rather than twitching
        // with every frame's correction.
        constexpr float kTurnSmoothingSeconds = 0.2F;
        float           g_turnAcceleration = 0.0F;

        // The slip angle, smoothed the same way, and only trusted with real speed: slower than
        // kSlipStartSpeed (m/s) the direction of travel is noise.
        constexpr float kSlipSmoothingSeconds = 0.15F;
        constexpr float kSlipStartSpeed = 0.5F;
        constexpr float kSlipFullSpeed = 2.0F;
        float           g_slip = 0.0F;

        // Off the ground: the highest the player has been since leaving it.
        std::optional<float> g_airPeak;

        // Out of stamina; logged when it changes.
        bool g_tired = false;

        // Flow: main thread only, in memory only (a save loading empties it). g_flowView is what the bar shows.
        Core::FlowState             g_flow;
        std::optional<RE::NiPoint3> g_flowLast;  // where the player was last frame, skiing
        std::mutex                  g_flowLock;
        FlowView                    g_flowView;

        Core::FlowSettings FlowSettingsOf(const Core::SkiSettings& physics)
        {
            return { .fillMetres = physics.flowFillMetres, .reserveMetres = physics.flowReserveMetres };
        }

        void PublishFlow(const Core::SkiSettings& physics, bool filled, bool absorbed)
        {
            std::scoped_lock lock(g_flowLock);
            g_flowView.enabled = physics.flow;
            g_flowView.full = g_flow.Full();
            g_flowView.shown = Core::FlowShown(g_flow, FlowSettingsOf(physics));
            g_flowView.fills += filled ? 1 : 0;
            g_flowView.absorbs += absorbed ? 1 : 0;
        }

        // A landing or crash as hard as falling this many metres, that would hurt or knock you down: true if a
        // full flow bar takes it instead.
        bool FlowTakes(RE::PlayerCharacter* player, float metres, const Core::SkiSettings& physics, const char* what)
        {
            if (!physics.flow || !Core::FlowAbsorb(g_flow, metres)) {
                return false;
            }
            spdlog::info("flow: took a {} like a {:.1f}m fall; {:.1f}m left{}", what, metres, g_flow.reserve,
                g_flow.Full() ? "" : ", empty");
            if (!g_flow.Full()) {
                Sounds::PlayFlow(player);
            }
            PublishFlow(physics, false, true);
            return true;
        }

        void SetOverlay(const OverlayData& overlay)
        {
            std::scoped_lock lock(g_overlayLock);
            g_overlay = overlay;
        }

        void SetBody(const Body& body)
        {
            std::scoped_lock lock(g_overlayLock);
            g_body = body;
        }

        void SetCommand(const Command& command)
        {
            std::scoped_lock lock(g_commandLock);
            g_command = command;
        }

        // Stopped, with the skis still on: the game stands the player still (a conversation, including one
        // an NPC starts) or plays its own animation (bleeding out).
        constexpr const char* kConversation = "conversation";
        constexpr const char* kDowned = "downed";
        constexpr const char* kWater = "water";

        // Bleeding out, either the game's own way or through the animation alone, as revive mods such as
        // PlayerSelfRevive do.
        bool Downed(RE::PlayerCharacter* player)
        {
            if (player->AsActorState()->IsBleedingOut()) {
                return true;
            }
            bool bleeding = false;
            return player->GetGraphVariableBool("IsBleedingOut", bleeding) && bleeding;
        }

        const char* InactiveReason(RE::PlayerCharacter* player, RE::bhkCharacterController* controller,
            const RE::TESObjectARMO* skis)
        {
            if (!Settings::ModEnabled()) {
                return "mod disabled";
            }
            if (!skis) {
                return "no skis";
            }
            if (!controller) {
                return "no character controller";
            }
            if (Downed(player)) {
                return kDowned;
            }
            const auto* state = player->AsActorState();
            if (state->GetLifeState() != RE::ACTOR_LIFE_STATE::kAlive) {
                return "not alive";
            }
            if (player->IsInWater() || state->IsSwimming()) {
                return kWater;
            }
            if (player->IsOnMount()) {
                return "mounted";
            }
            if (player->IsInRagdollState()) {
                return "ragdoll";
            }
            // The get-up animation turns the character, which the chase camera would read as looking around.
            if (state->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal) {
                return "getting up";
            }
            if (player->IsInKillMove()) {
                return "kill move";
            }
            if (state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
                return "furniture";
            }
            if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
                return kConversation;
            }
            return nullptr;
        }

        // Whichever of triggers or mouse buttons last asked for edge decides how the edge releases.
        bool g_edgeFromButtons = true;

        // The steering stick, smoothed so thumb twitches don't become ski twitches.
        Core::Vec2 g_stick{};

        Core::Vec2 g_heldKeys{};
        float      g_keyHold = 0.0F;  // seconds the same keys have been held
        float      g_keyPush = 0.0F;  // seconds any keys have been held

        Core::Vec2 FreshStick(float x, float y, float age)
        {
            return age >= 0.0F && age < kStaleStickSeconds ? Core::Vec2{ x, y } : Core::Vec2{};
        }

        // Edge: triggers (analog) or mouse buttons. Skid: its bindings. Aim: the left stick or W/A/S/D, relative
        // to where the camera looks (viewYaw) if looking around steers for that device, otherwise to where the
        // chase camera points by itself (chaseYaw).
        void ReadControls(Core::SkiInput& ski, float viewYaw, float chaseYaw, float delta, const Core::SkiSettings& physics)
        {
            const auto  input = Input::Get();
            const float triggers = input.triggerRight - input.triggerLeft;
            const float buttons = (input.mouseRight ? 1.0F : 0.0F) - (input.mouseLeft ? 1.0F : 0.0F);
            float       edge = 0.0F;
            if (input.triggerLeft > kTriggerDeadzone || input.triggerRight > kTriggerDeadzone) {
                g_edgeFromButtons = false;
                edge = triggers;
            } else if (input.mouseLeft || input.mouseRight) {
                g_edgeFromButtons = true;
                edge = buttons;
            }
            ski.edgeTarget = edge;
            ski.edgeFromButtons = g_edgeFromButtons;
            ski.skidHeld = input.skid;

            const Core::Vec2 stick = FreshStick(input.stickX, input.stickY, input.stickAge);
            const float      blend = 1.0F - std::exp(-std::max(0.0F, delta) / kStickSmoothingSeconds);
            g_stick = { g_stick.x + (stick.x - g_stick.x) * blend, g_stick.y + (stick.y - g_stick.y) * blend };

            const Core::Vec2 keys{ (input.right ? 1.0F : 0.0F) - (input.left ? 1.0F : 0.0F),
                (input.forward ? 1.0F : 0.0F) - (input.back ? 1.0F : 0.0F) };
            g_keyHold = keys.x != g_heldKeys.x || keys.y != g_heldKeys.y ? 0.0F : g_keyHold + std::max(0.0F, delta);
            g_heldKeys = keys;
            g_keyPush = keys.x == 0.0F && keys.y == 0.0F ? 0.0F : g_keyPush + std::max(0.0F, delta);
            const auto keyStrengthAfter = [](float seconds) {
                const float build = std::min(1.0F, seconds / kKeyAimBuildSeconds);
                return build * build;
            };
            const float keyStrength = keyStrengthAfter(g_keyHold);
            const float keyPush = keyStrengthAfter(g_keyPush);

            if (keys.x != 0.0F || keys.y != 0.0F) {
                ski.aimYaw = (Settings::LookSteersKeyboardMouse() ? viewYaw : chaseYaw) + std::atan2(keys.x, keys.y);
                ski.aimStrength = keyStrength;
                ski.pushStrength = keyPush;
            } else {
                ski.aimYaw = (Settings::LookSteersGamepad() ? viewYaw : chaseYaw) + std::atan2(g_stick.x, g_stick.y);
                ski.aimStrength = Core::AimStrength(g_stick.x, g_stick.y, physics.aimDeadzone);
            }
        }

        // The engine's velocity in the model's convention: horizontal direction, speed along the ground.
        Core::Vec2 Observed(const Core::Vec3& velocity, bool grounded)
        {
            const float horizontal = Core::HorizontalLength(velocity);
            if (horizontal < 1e-4F) {
                return {};
            }
            const float speed = grounded ? Core::Length(velocity) : horizontal;
            return { velocity.x / horizontal * speed, velocity.y / horizontal * speed };
        }

        // Set when the player jumps; cleared once back on the ground.
        bool g_jumped = false;

        // Off the ground: the engine's velocity over the last few frames, to judge the landing by and carry into
        // it (the speed in the air is mostly falling, which the model doesn't follow). Several, because the engine
        // stops the fall the frame it touches down but only says it's supported the frame after.
        struct AirVelocities
        {
            static constexpr std::size_t kFrames = 3;
            std::array<Core::Vec3, kFrames> recent{};
            std::size_t                     count{ 0 };
            Core::Vec3                      takeoff{};  // the first in the air

            void Add(const Core::Vec3& velocity)
            {
                if (count == 0) {
                    takeoff = velocity;
                }
                std::move_backward(recent.begin(), recent.end() - 1, recent.end());
                recent[0] = velocity;
                count = std::min(count + 1, kFrames);
            }
            void ReplaceLatest(const Core::Vec3& velocity)
            {
                if (count > 0) {
                    recent[0] = velocity;
                }
            }
            // The one meeting a slope with this normal hardest: what it was going before the touchdown.
            [[nodiscard]] Core::Vec3 Hardest(const Core::Vec3& normal) const
            {
                return *std::max_element(recent.begin(), recent.begin() + count, [&](const auto& a, const auto& b) {
                    return Core::SpeedInto(a, normal) < Core::SpeedInto(b, normal);
                });
            }
            void Reset() { count = 0; }
        };
        AirVelocities g_airVelocity;

        // Briefly unsupported after skiing over a crest (not a jump): keep the skier on the slope. Returns the
        // velocity set, if it was.
        std::optional<Core::Vec3> HugGround(RE::bhkCharacterController* controller, const Core::Vec3& velocity,
            const Core::GroundEstimate& ground, const Core::SkiSettings& physics, float delta)
        {
            const bool supported =
                controller->surfaceInfo.supportedState.get() == RE::hkpSurfaceInfo::SupportedState::kSupported;
            if (controller->context.currentState == RE::hkpCharacterStateTypes::kJumping) {
                g_jumped = true;
            } else if (supported) {
                g_jumped = false;
            }
            if (supported || g_jumped || !ground.hasNormal || ground.unsupportedSeconds >= physics.groundHugSeconds) {
                return std::nullopt;
            }
            const Core::Vec3 hugged{ velocity.x, velocity.y, Core::GroundHugVerticalSpeed(velocity, ground.normal, delta, physics) };
            controller->SetLinearVelocityImpl(RE::hkVector4(hugged.x, hugged.y, hugged.z, 0.0F));
            return hugged;
        }

        // Skyrim's fall damage counts the height dropped since leaving the ground, which on skis is wrong both
        // ways: skiing fast down a steep slope (briefly off the snow many times a second) counts as one long
        // fall, and a big air onto a steep face hurts however softly it lands. So on skis the game counts no
        // fall at all: while off the ground the fall restarts where the skier is, every frame. The controller
        // keeps that height in game units (seen on 1.6.640); the first time off the ground says if it doesn't.
        void CancelFall(RE::bhkCharacterController* controller, const RE::NiPoint3& position)
        {
            if (controller->surfaceInfo.supportedState.get() == RE::hkpSurfaceInfo::SupportedState::kSupported) {
                return;
            }
            static bool checked = false;
            if (!checked && std::fabs(position.z) > 500.0F && controller->fallStartHeight != 0.0F) {
                checked = true;
                const float start = controller->fallStartHeight;
                if (std::fabs(start - position.z) > std::fabs(start - position.z * RE::bhkWorld::GetWorldScale())) {
                    spdlog::warn("fall start height looks like Havok units (start={:.1f}, z={:.1f}): game fall "
                                 "damage on skis may be wrong in this game version", start, position.z);
                }
            }
            controller->fallStartHeight = position.z;
        }

        // Impacts: speed the world took away all at once. Logged, and crashes into something steep hurt (landings
        // are judged at touchdown instead, see LandingDamage).
        Core::ImpactTracker  g_impacts;
        std::optional<float> g_impactHealth;  // health the frame before an impact started
        std::optional<float> g_lastHealth;
        RE::NiPoint3         g_lastPosition;
        // Moving further than this in one frame is a teleport or load, not skiing.
        constexpr float kTeleportUnits = 1000.0F;
        // What was hit is looked for this far along the speed lost, from the body's centre.
        constexpr float kImpactProbeUnits = 150.0F;
        // Lost at least this much sideways (m/s), the ray looks sideways for what was hit, not along the loss.
        constexpr float kSidewaysLoss = 1.0F;

        float Degrees(float radians) { return radians * 180.0F / std::numbers::pi_v<float>; }

        std::string_view LayerName(RE::COL_LAYER layer)
        {
            switch (layer) {
            case RE::COL_LAYER::kTerrain:        return "terrain";
            case RE::COL_LAYER::kGround:         return "ground";
            case RE::COL_LAYER::kStatic:         return "static";
            case RE::COL_LAYER::kAnimStatic:     return "animstatic";
            case RE::COL_LAYER::kTrees:          return "tree";
            case RE::COL_LAYER::kClutter:        return "clutter";
            case RE::COL_LAYER::kProps:          return "props";
            case RE::COL_LAYER::kBiped:
            case RE::COL_LAYER::kCharController: return "actor";
            default:                             return "other";
            }
        }

        // Things you can crash into: not people or loose objects.
        bool Solid(RE::COL_LAYER layer)
        {
            switch (layer) {
            case RE::COL_LAYER::kClutter:
            case RE::COL_LAYER::kProps:
            case RE::COL_LAYER::kBiped:
            case RE::COL_LAYER::kCharController: return false;
            default:                             return true;
            }
        }

        struct SurfaceHit
        {
            RE::COL_LAYER layer;
            float         slopeDegrees;
        };

        // What's along the given direction (unit) from the body's centre: rays hit what the player's character
        // controller collides with, but not the player.
        std::optional<SurfaceHit> Probe(RE::PlayerCharacter* player, RE::bhkCharacterController* controller,
            float bodyZ, const Core::Vec3& direction)
        {
            auto* cell = player->GetParentCell();
            auto* world = cell ? cell->GetbhkWorld() : nullptr;
            if (!world) {
                return std::nullopt;
            }
            RE::CFilter own;
            controller->GetCollisionFilterInfo(own);
            const auto      p = player->GetPosition();
            const float     scale = RE::bhkWorld::GetWorldScale();
            const auto      reach = [&](float along) { return along * kImpactProbeUnits * scale; };
            RE::bhkPickData pick;
            pick.rayInput.from = RE::hkVector4(p.x * scale, p.y * scale, bodyZ * scale, 0.0F);
            pick.rayInput.to = RE::hkVector4(p.x * scale + reach(direction.x), p.y * scale + reach(direction.y),
                bodyZ * scale + reach(direction.z), 0.0F);
            pick.rayInput.filterInfo.filter =
                (own.GetSystemGroup() << 16) | static_cast<std::uint32_t>(RE::COL_LAYER::kCharController);
            world->PickObject(pick);
            const auto* collidable = pick.rayOutput.rootCollidable;
            if (!pick.rayOutput.HasHit() || !collidable) {
                return std::nullopt;
            }
            const auto normal = ToVec3(pick.rayOutput.normal);
            return SurfaceHit{ collidable->GetCollisionLayer(), Degrees(std::acos(std::clamp(normal.z, -1.0F, 1.0F))) };
        }

        std::string HitName(const std::optional<SurfaceHit>& hit)
        {
            return hit ? fmt::format("{} at {:.0f}", LayerName(hit->layer), hit->slopeDegrees) : std::string("nothing found");
        }

        Core::FallCurve GameFallCurve()
        {
            auto* settings = RE::GameSettingCollection::GetSingleton();
            const auto value = [&](const char* name, float fallback) {
                auto* setting = settings ? settings->GetSetting(name) : nullptr;
                return setting ? setting->GetFloat() : fallback;
            };
            return { value("fJumpFallHeightMin", 600.0F), value("fJumpFallHeightMult", 0.1F),
                value("fJumpFallHeightExponent", 1.65F) };
        }

        void LogFallSettings()
        {
            const auto curve = GameFallCurve();
            spdlog::info("game fall damage curve: fJumpFallHeightMin={:.3f} fJumpFallHeightMult={:.3f} "
                         "fJumpFallHeightExponent={:.3f}",
                curve.minimumUnits, curve.mult, curve.exponent);
        }

        // Health damage for a hit as hard as falling this many metres, scaled.
        float ImpactDamage(float metres, float scale)
        {
            return std::max(0.0F, scale) * Core::FallDamage(metres * kUnitsPerMetre, GameFallCurve());
        }

        // God mode (tgm): a hit that would hurt or knock you down does neither, and leaves the flow bar alone.
        bool GodModeTakes(const char* what)
        {
            if (!RE::PlayerCharacter::IsGodMode()) {
                return false;
            }
            spdlog::info("god mode: ignored a {}", what);
            return true;
        }

        void Hurt(RE::PlayerCharacter* player, float damage)
        {
            if (damage > 0.0F) {
                player->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kHealth, damage);
            }
        }

        // Set once a tumble is asked for, until skiing starts again, so one crash throws the skier once.
        bool g_tumbling = false;
        // From a tumble until back on their feet on solid ground for kTumbleSettleSeconds: skiing is off by then
        // (ragdoll, skis off), so the game would count the whole tumble down the mountain as one fall.
        bool                 g_tumbleGuard = false;
        float                g_tumbleSettled = 0.0F;
        float                g_tumbleSeconds = 0.0F;
        std::optional<float> g_tumbleHealth;
        constexpr float      kTumbleSettleSeconds = 1.0F;
        float g_tumbleRagdollSeconds = 0.0F;
        float g_tumbleLogSeconds = 0.0F;
        // Once the slide is over the ragdoll's speed across the ground (and up, and its spin) falls by e every
        // 1/kTumbleBrakeRate seconds, and to nothing below kTumbleStopSpeed (Havok units a second, about m/s).
        // Falling is left to gravity, or a body still in the air would hang there, until the whole ragdoll is
        // slower than kTumbleRestSpeed with the ground right under it: then it's creeping down the slope, and the
        // game only gets the player up once it's still.
        constexpr float kTumbleBrakeRate = 15.0F;
        constexpr float kTumbleStopSpeed = 0.5F;
        constexpr float kTumbleRestSpeed = 1.0F;
        float           g_ragdollFastest = 0.0F;
        // The push comes from this far behind the skier (game units), along the way they were going.
        constexpr float kTumblePushUnits = 50.0F;

        // Once: the game's own settings that might decide how long a knockdown lasts.
        void LogKnockdownSettings()
        {
            static bool logged = false;
            auto*       settings = RE::GameSettingCollection::GetSingleton();
            if (logged || !settings) {
                return;
            }
            logged = true;
            for (auto& item : settings->settings) {
                const char*  name = item.first;
                RE::Setting* setting = item.second;
                if (!name || !setting) {
                    continue;
                }
                std::string lower(name);
                std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return std::tolower(c); });
                if (lower.find("ragdoll") == std::string::npos && lower.find("knock") == std::string::npos &&
                    lower.find("getup") == std::string::npos) {
                    continue;
                }
                switch (setting->GetType()) {
                case RE::Setting::Type::kFloat:   spdlog::info("game setting {} = {}", name, setting->GetFloat()); break;
                case RE::Setting::Type::kInteger: spdlog::info("game setting {} = {}", name, setting->GetInteger()); break;
                case RE::Setting::Type::kBool:    spdlog::info("game setting {} = {}", name, setting->GetBool()); break;
                default:                          spdlog::info("game setting {}", name); break;
                }
            }
        }

        struct RagdollSpeed
        {
            float fastest{ 0.0F };        // m/s
            float fastestAcross{ 0.0F };  // m/s, horizontal
        };

        // Brakes every body in the player's ragdoll by factor (1 leaves it), falling too if settling. Returns the
        // speeds before.
        RagdollSpeed BrakeRagdoll(RE::PlayerCharacter* player, float factor, bool settling)
        {
            auto* root = player->Get3D(false);
            auto* cell = player->GetParentCell();
            auto* world = cell ? cell->GetbhkWorld() : nullptr;
            RagdollSpeed speeds;
            if (!root || !world) {
                return speeds;
            }
            RE::BSWriteLockGuard lock(world->worldLock);
            RE::BSVisit::TraverseScenegraphCollision(root, [&](RE::bhkNiCollisionObject* object) {
                auto* body = netimmerse_cast<RE::bhkRigidBody*>(object->body.get());
                auto* rigid = body ? body->GetRigidBody() : nullptr;
                if (!rigid) {
                    return RE::BSVisit::BSVisitControl::kContinue;
                }
                const auto& motion = rigid->motion;
                const auto  velocity = ToVec3(motion.linearVelocity);
                const float across = Core::HorizontalLength(velocity);
                speeds.fastest = std::max(speeds.fastest, Core::Length(velocity));
                speeds.fastestAcross = std::max(speeds.fastestAcross, across);
                if (factor >= 1.0F) {
                    return RE::BSVisit::BSVisitControl::kContinue;
                }
                const bool  moving = across > 1e-3F || velocity.z > 1e-3F || (settling && velocity.z < -1e-3F);
                const float spin = motion.angularVelocity.Length3();
                if (moving) {
                    const float k = across * factor < kTumbleStopSpeed ? 0.0F : factor;
                    const float braked = std::fabs(velocity.z) * factor < kTumbleStopSpeed ? 0.0F : velocity.z * factor;
                    const float up = velocity.z > 0.0F || settling ? braked : velocity.z;
                    body->SetLinearVelocity(RE::hkVector4(velocity.x * k, velocity.y * k, up, 0.0F));
                }
                if (spin > 1e-3F) {
                    body->SetAngularVelocity(motion.angularVelocity * RE::hkVector4(spin * factor < kTumbleStopSpeed ? 0.0F : factor));
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
            return speeds;
        }

        bool Alive(RE::PlayerCharacter* player)
        {
            return player->AsActorState()->GetLifeState() == RE::ACTOR_LIFE_STATE::kAlive &&
                   player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth) > 0.0F;
        }

        // A hard hit throws the skier into the game's own knockdown (ragdoll, then getting up), pushed on the way
        // they were going. Queued for after the player's update rather than in the middle of it.
        void Tumble(RE::PlayerCharacter* player, const Core::Vec3& travel, float hit, float minSpeed, bool hurt,
            const Core::SkiSettings& physics)
        {
            if (!physics.crashRagdoll || hit < minSpeed || g_tumbling || !Alive(player) ||
                RE::PlayerCharacter::IsGodMode()) {
                return;
            }
            g_tumbling = true;
            g_tumbleGuard = true;
            if (physics.flow && !g_flow.Full() && g_flow.descended > 0.0F) {
                spdlog::info("flow: a tumble at {:.0f}m of {:.0f}, starting over", g_flow.descended, physics.flowFillMetres);
            }
            Core::FlowFall(g_flow);
            g_flowLast.reset();
            g_tumbleSettled = 0.0F;
            g_tumbleSeconds = 0.0F;
            g_tumbleHealth = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth);
            g_tumbleRagdollSeconds = 0.0F;
            g_tumbleLogSeconds = 0.0F;
            g_ragdollFastest = 0.0F;
            Camera::StartTumble();
            Camera::WatchState(player, "tumble start", true);
            LogKnockdownSettings();
            const float      horizontal = Core::HorizontalLength(travel);
            const Core::Vec3 direction = horizontal > 1e-3F ? Core::Vec3{ travel.x / horizontal, travel.y / horizontal, 0.0F } :
                                                              Core::Vec3{};
            const float      magnitude = std::max(0.0F, physics.ragdollThrow) * Core::Length(travel);
            const bool       removeSkis = hurt ? physics.hurtTumbleRemovesSkis : physics.harmlessTumbleRemovesSkis;
            const float      kept = std::clamp(physics.ragdollSpeedKept, 0.0F, 1.0F);
            spdlog::info("tumble: hit {:.1f}m/s at {:.0f}mph, throw {:.1f}, keeping {:.0f}% of the speed{}", hit,
                Core::Length(travel) * 2.23694F, magnitude, kept * 100.0F, removeSkis ? ", skis off" : "");
            SKSE::GetTaskInterface()->AddTask([direction, magnitude, removeSkis, kept] {
                auto* player = RE::PlayerCharacter::GetSingleton();
                auto* process = player ? player->GetActorRuntimeData().currentProcess : nullptr;
                if (!process || !process->high || !Alive(player)) {
                    return;
                }
                if (auto* controller = player->GetCharController()) {
                    RE::hkVector4 velocity;
                    controller->GetLinearVelocityImpl(velocity);
                    const auto before = ToVec3(velocity);
                    controller->SetLinearVelocityImpl(
                        RE::hkVector4(before.x * kept, before.y * kept, before.z * kept, 0.0F));
                    spdlog::info("tumble: body speed {:.1f} -> {:.1f}m/s", Core::Length(before),
                        Core::Length(before) * kept);
                }
                const auto position = player->GetPosition();
                const RE::NiPoint3 from{ position.x - direction.x * kTumblePushUnits,
                    position.y - direction.y * kTumblePushUnits, position.z };
                Sounds::MuteRagdollLoop(true);
                process->KnockExplosion(player, from, magnitude);
                if (auto* skis = removeSkis ? Skis::Worn(player) : nullptr) {
                    RE::ActorEquipManager::GetSingleton()->UnequipObject(player, skis);
                }
            });
        }

        // While guarding a tumble, the game counts no fall: the fall restarts where the player is every frame.
        void GuardTumble(RE::PlayerCharacter* player, RE::bhkCharacterController* controller, float delta)
        {
            if (!g_tumbleGuard || !controller) {
                return;
            }
            const auto position = player->GetPosition();
            controller->fallStartHeight = position.z;
            g_tumbleSeconds += std::max(0.0F, delta);
            Camera::WatchState(player, fmt::format("tumble {:.2f}s camera changed", g_tumbleSeconds).c_str());
            if (player->IsInRagdollState()) {
                const float slide = Settings::Physics().tumbleSlideSeconds;
                g_tumbleRagdollSeconds += std::max(0.0F, delta);
                const bool  braking = g_tumbleRagdollSeconds >= slide;
                const bool  settling = braking && g_ragdollFastest < kTumbleRestSpeed &&
                                      Probe(player, controller, position.z + 30.0F, { 0.0F, 0.0F, -1.0F }).has_value();
                const auto  speeds = BrakeRagdoll(player,
                    braking ? std::exp(-kTumbleBrakeRate * std::max(0.0F, delta)) : 1.0F, settling);
                g_ragdollFastest = speeds.fastest;
                if (Settings::DetailedLog() && g_tumbleSeconds >= g_tumbleLogSeconds) {
                    g_tumbleLogSeconds = g_tumbleSeconds + 0.5F;
                    spdlog::info("tumble {:.1f}s: ragdoll fastest {:.1f}m/s ({:.1f} across){} | {}", g_tumbleSeconds,
                        speeds.fastest, speeds.fastestAcross, settling ? ", settling" : braking ? ", braking" : "",
                        Camera::Describe(player));
                }
            }
            const bool down = player->IsInRagdollState() ||
                              player->AsActorState()->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal;
            const bool supported =
                controller->surfaceInfo.supportedState.get() == RE::hkpSurfaceInfo::SupportedState::kSupported;
            if (down || !supported) {
                g_tumbleSettled = 0.0F;
                return;
            }
            g_tumbleSettled += std::max(0.0F, delta);
            if (g_tumbleSettled >= kTumbleSettleSeconds) {
                g_tumbleGuard = false;
                Sounds::MuteRagdollLoop(false);
                if (!Downed(player)) {
                    Camera::LeaveKnockdownCamera(player);
                }
                spdlog::info("tumble over after {:.1f}s | health {:.0f} -> {:.0f} | {}", g_tumbleSeconds,
                    g_tumbleHealth.value_or(0.0F), player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kHealth),
                    Camera::Describe(player));
            }
        }

        // Touching down with this velocity (the last in the air) on a slope with this normal, powder 0..1 deep.
        void LandingDamage(RE::PlayerCharacter* player, const Core::Vec3& velocity, const Core::Vec3& takeoff,
            const Core::Vec3& normal, float powder, const Core::SkiSettings& physics)
        {
            const float slope =
                Degrees(std::acos(std::clamp(normal.z / std::max(Core::Length(normal), 1e-4F), -1.0F, 1.0F)));
            if (slope >= physics.crashMinSurfaceDegrees) {
                return;  // a face that steep is a crash, judged as one
            }
            const float into = Core::SpeedInto(velocity, normal);
            const float takeoffInto = Core::SpeedInto(takeoff, normal);
            const float metres = Core::LandingFallMetres(into, takeoffInto, physics.gravity);
            const float cushion = physics.powder ? physics.powderLandingMetres * powder : 0.0F;
            const float cushioned = std::max(0.0F, metres - cushion);
            // Powder takes its share of the tumble too: what's left is as hard as hitting at this speed.
            const float hit = std::sqrt(2.0F * std::max(0.0F, physics.gravity) * cushioned);
            float       damage = physics.impactDamage ? ImpactDamage(cushioned, physics.landingDamageScale) : 0.0F;
            const bool  harmful = damage > 0.0F || (physics.crashRagdoll && hit >= physics.landingTumbleSpeed);
            const bool  spared = harmful && (GodModeTakes("landing") || FlowTakes(player, cushioned, physics, "landing"));
            if (spared) {
                damage = 0.0F;
            }
            if (metres < 3.0F && damage <= 0.0F) {
                return;
            }
            auto*       values = player->AsActorValueOwner();
            const float health = values->GetActorValue(RE::ActorValue::kHealth);
            Hurt(player, damage);
            if (!spared) {
                Tumble(player, velocity, hit, physics.landingTumbleSpeed, damage > 0.0F, physics);
            }
            spdlog::info("landing: {:.1f}m/s into a {:.0f} degree slope ({:.1f} at takeoff) at {:.0f}mph | like a "
                         "{:.1f}m fall ({:.1f}m after {:.0f}% powder) -> damage {:.0f} | health {:.0f} -> {:.0f}",
                into, slope, takeoffInto, Core::Length(velocity) * 2.23694F, metres, cushioned,
                physics.powder ? powder * 100.0F : 0.0F, damage, health,
                values->GetActorValue(RE::ActorValue::kHealth));
        }

        // This frame's crash, for its sound: m/s lost and what was hit.
        struct CrashSound
        {
            float          speed{ 0.0F };
            Sounds::Ground ground{ Sounds::Ground::Stone };
        };
        CrashSound g_crashSound;

        void TrackImpacts(RE::PlayerCharacter* player, RE::bhkCharacterController* controller,
            const RE::NiPoint3& position, const Core::Vec3& velocity, bool airborne,
            const std::optional<Core::Vec3>& hugged, float bodyZ, const Core::SkiSettings& physics, float delta)
        {
            auto* values = player->AsActorValueOwner();
            if (position.GetDistance(g_lastPosition) > kTeleportUnits) {
                g_impacts.Reset();
                g_impactHealth.reset();
                g_lastHealth.reset();
            }
            g_lastPosition = position;
            const float health = values->GetActorValue(RE::ActorValue::kHealth);
            const bool  wasInImpact = g_impacts.InImpact();
            const auto  impact = g_impacts.Update({ velocity, airborne, delta });
            if (hugged) {
                g_impacts.Override(*hugged);
            }
            if (!wasInImpact && g_impacts.InImpact()) {
                g_impactHealth = g_lastHealth.value_or(health);
            }
            g_lastHealth = health;
            if (!impact) {
                return;
            }
            const float lost = Core::Length(impact->lost);
            const float sideways = Core::HorizontalLength(impact->lost);
            const auto  direction = sideways >= kSidewaysLoss ?
                                        Core::Vec3{ impact->lost.x / sideways, impact->lost.y / sideways, 0.0F } :
                                        Core::Vec3{ impact->lost.x / lost, impact->lost.y / lost, impact->lost.z / lost };
            const auto  hit = Probe(player, controller, bodyZ, direction);
            const bool  crash = hit && Solid(hit->layer) && hit->slopeDegrees >= physics.crashMinSurfaceDegrees &&
                               sideways >= kSidewaysLoss;
            const float metres = Core::EquivalentFallMetres(sideways, physics.gravity);
            float       damage = crash && physics.impactDamage ? ImpactDamage(metres, physics.crashDamageScale) : 0.0F;
            const bool  harmful = crash && (damage > 0.0F || (physics.crashRagdoll && sideways >= physics.crashTumbleSpeed));
            const bool  spared = harmful && (GodModeTakes("crash") || FlowTakes(player, metres, physics, "crash"));
            if (spared) {
                damage = 0.0F;
            }
            Hurt(player, damage);
            if (crash) {
                g_crashSound = { sideways, hit->layer == RE::COL_LAYER::kTrees || hit->layer == RE::COL_LAYER::kProps ?
                                               Sounds::Ground::Wood :
                                               Sounds::Ground::Stone };
                if (!spared) {
                    Tumble(player, impact->before, sideways, physics.crashTumbleSpeed, damage > 0.0F, physics);
                }
            }
            const float before = Core::Length(impact->before);
            if (!crash && !Settings::DetailedLog()) {
                g_impactHealth.reset();
                return;
            }
            spdlog::info("impact: lost {:.1f}m/s ({:+.1f} vertical, {:.1f} sideways) over {} frame{} of {:.1f}m/s "
                         "({:.0f}mph) | hit {} | {} | health {:.0f} -> {:.0f}",
                lost, impact->lost.z, sideways, impact->frames, impact->frames == 1 ? "" : "s", before,
                before * 2.23694F, HitName(hit),
                crash ? fmt::format("crash like a {:.1f}m fall -> damage {:.0f}", metres, damage) : std::string("no crash"),
                g_impactHealth.value_or(health), values->GetActorValue(RE::ActorValue::kHealth));
            g_impactHealth.reset();
        }

        // The game moves the player's position with the (horizontal) movement and only catches up with the Havok
        // body's height every few frames, so descending a steep slope fast the character rises off the snow and
        // drops back about 12 times a second. Keep it at the body's height instead. The offset between the two
        // is learned where the game has them agree: on gentle ground, and the frames it catches up.
        struct HeightSync
        {
            std::optional<float> offset;
            std::optional<float> lastZ;  // the game's own value, before any lowering
            std::optional<float> lastBodyZ;
            float                candidate{ 0.0F };
            int                  agreeing{ 0 };
        };
        HeightSync g_height;

        // A skating stride is a single frame; the overlay shows skating for this long after one.
        constexpr float kSkatingShownSeconds = 0.8F;
        float           g_sinceStride = kSkatingShownSeconds;

        constexpr float kGentleNormalZ = 0.966F;  // cos 15 degrees
        // Game units per second, times the frame time, so they mean the same at any frame rate: the position's
        // and the body's height changing this close together agree; the position dropping this much faster than
        // the body is the game catching up.
        constexpr float kAgreeSpeed = 30.0F;
        constexpr float kCatchUpSpeed = 600.0F;
        // An offset is only taken once this many readings in a row agree within kLearnTolerance (game units);
        // single readings mid-landing can be well off, and an offset too low buries the feet.
        constexpr int   kLearnReadings = 6;
        constexpr float kLearnTolerance = 1.0F;

        float BodyHeight(RE::bhkCharacterController* controller)
        {
            RE::hkVector4 body;
            controller->GetPositionImpl(body, false);
            return body.quad.m128_f32[2] / RE::bhkWorld::GetWorldScale();
        }

        float SyncHeight(RE::PlayerCharacter* player, RE::bhkCharacterController* controller,
            const Core::GroundEstimate& ground, const Core::SkiSettings& physics, float delta)
        {
            const float bodyZ = BodyHeight(controller);
            float&      z = player->data.location.z;
            const float gameZ = z;
            const bool  gentleSlope = ground.hasNormal && ground.normal.z >= kGentleNormalZ;
            if (g_height.lastZ && g_height.lastBodyZ && delta > 0.0F) {
                const float moved = gameZ - *g_height.lastZ;
                const float bodyMoved = bodyZ - *g_height.lastBodyZ;
                const bool  supported =
                    controller->surfaceInfo.supportedState.get() == RE::hkpSurfaceInfo::SupportedState::kSupported;
                const bool gentle = supported && gentleSlope && std::fabs(moved - bodyMoved) < kAgreeSpeed * delta;
                // Catching up on steep ground is only trusted to get a first offset before any gentle ground.
                const bool caughtUp = !g_height.offset && moved - bodyMoved < -kCatchUpSpeed * delta;
                if (gentle || caughtUp) {
                    const float learned = gameZ - bodyZ;
                    if (g_height.agreeing > 0 && std::fabs(learned - g_height.candidate) <= kLearnTolerance) {
                        ++g_height.agreeing;
                    } else {
                        g_height.candidate = learned;
                        g_height.agreeing = 1;
                    }
                    if (g_height.agreeing >= kLearnReadings) {
                        if (Settings::DetailedLog() && (!g_height.offset || std::fabs(*g_height.offset - learned) > 2.0F)) {
                            spdlog::info("character sits {:.1f} units from the physics body ({})", learned,
                                gentle ? "gentle ground" : "caught up");
                        }
                        g_height.offset = learned;
                    }
                } else if (gentleSlope) {
                    g_height.agreeing = 0;
                }
            }

            // Gentle ground is left to the game, so its own heights there stay clean to learn from.
            float lowered = 0.0F;
            if (physics.smoothHeight && g_height.offset && ground.contact == Core::Contact::Grounded && !gentleSlope) {
                const float target = bodyZ + *g_height.offset;
                if (z > target) {
                    lowered = z - target;
                    z = target;
                }
            }
            g_height.lastZ = gameZ;
            g_height.lastBodyZ = bodyZ;
            return lowered;
        }

        // The snow under the skis is read a few times a second (and at every touchdown), then blended in over the
        // distance skied so patchy ground eases in and out.
        constexpr float kSurfaceSeconds = 0.1F;
        constexpr float kSnowBlendMetres = 3.0F;
        struct Snow
        {
            Surface::Reading reading;
            float            readAge{ kSurfaceSeconds };
            float            powder{ 1.0F };
            RE::NiPoint3     lastPosition;
            std::string      logged;
        };
        Snow g_snow;

        void LogSurface(const Surface::Reading& reading)
        {
            if (!Settings::DetailedLog()) {
                return;
            }
            const auto stem = reading.texture.substr(reading.texture.find_last_of("\\/") + 1);
            // Rounded, so it's only logged when it changes enough to matter.
            auto line = fmt::format(
                "surface: {} land ({} below, {:.0f} above land) {} ({}) underfoot {} | powder {:.0f}%",
                reading.onLand ? "on" : "off", LayerName(reading.below), std::round(reading.aboveLandUnits / 10.0F) * 10.0F,
                Surface::MaterialName(reading.land), stem.empty() ? "-" : stem,
                Surface::MaterialName(reading.underfoot), std::round(reading.powderCover * 4.0F) * 25.0F);
            if (line != g_snow.logged) {
                spdlog::info("{}", line);
                g_snow.logged = std::move(line);
            }
        }

        // How deep in powder the skis are, 0..1, kept in g_snow.
        void FollowSnow(RE::PlayerCharacter* player, RE::bhkCharacterController* controller,
            const RE::NiPoint3& position, bool grounded, bool snap, float delta, const Core::SkiSettings& physics)
        {
            g_snow.readAge += std::max(0.0F, delta);
            if (snap || g_snow.readAge >= kSurfaceSeconds) {
                g_snow.reading = Surface::Read(player, controller);
                g_snow.readAge = 0.0F;
                LogSurface(g_snow.reading);
            }
            const auto& reading = g_snow.reading;
            const float powder = !physics.autoPowder ? 1.0F : reading.onLand ? reading.powderCover : 0.0F;
            const float moved = position.GetDistance(g_snow.lastPosition) / kUnitsPerMetre;
            g_snow.lastPosition = position;
            if (snap) {
                g_snow.powder = powder;
            } else if (grounded) {
                g_snow.powder = Core::FollowOverDistance(g_snow.powder, powder, moved, kSnowBlendMetres);
            }
        }

        void SetYaw(RE::PlayerCharacter* player, float yaw)
        {
            if (!Tdm::SetPlayerYaw(yaw)) {
                player->SetHeading(yaw);
            }
        }
    }

    void Update(RE::PlayerCharacter* player, float delta)
    {
        auto* controller = player->GetCharController();
        auto* skis = Skis::Worn(player);
        GuardTumble(player, controller, delta);
        if (const char* reason = InactiveReason(player, controller, skis)) {
            if (g_debug.active) {
                spdlog::info("skiing inactive: {}", reason);
            }
            g_debug.active = false;
            g_debug.inactiveReason = reason;
            g_flowLast.reset();
            PublishFlow(Settings::Physics(), false, false);
            SetCommand({});
            SetOverlay({ .active = false });
            Sounds::Stop();
            if (g_tumbleGuard) {
                Camera::FilmTumble(player, delta, reason == kDowned);
            } else {
                Camera::Reset();
            }
            // The stance blends out from the last pose, so it keeps where the skis pointed.
            auto body = GetBody();
            body.active = false;
            body.airDropMetres = 0.0F;
            SetBody(body);
            Skis::Sync(player, reason == kConversation || reason == kDowned ? skis : nullptr);
            if (reason == kWater && skis) {
                SKSE::GetTaskInterface()->AddTask([] {
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    if (auto* skis = player ? Skis::Worn(player) : nullptr) {
                        RE::ActorEquipManager::GetSingleton()->UnequipObject(player, skis);
                        spdlog::info("skis off: water");
                    }
                });
            }
            return;
        }

        RE::hkVector4 havokVelocity;
        controller->GetLinearVelocityImpl(havokVelocity);
        const auto velocity = ToVec3(havokVelocity);
        const auto position = player->GetPosition();
        const bool starting = !g_debug.active;
        if (starting) {
            g_height.lastZ.reset();
            g_height.lastBodyZ.reset();
            g_ground.Reset();
            g_stick = {};
            g_height.agreeing = 0;
            g_sinceStride = kSkatingShownSeconds;
            g_slip = 0.0F;
            g_airPeak.reset();
            g_impacts.Reset();
            g_impactHealth.reset();
            g_lastHealth.reset();
            g_lastPosition = position;
            g_tumbling = false;
            static bool settingsLogged = false;
            if (!std::exchange(settingsLogged, true)) {
                LogFallSettings();
            }
        }
        const auto& ground = g_ground.Update({
            .supported = controller->surfaceInfo.supportedState.get() == RE::hkpSurfaceInfo::SupportedState::kSupported,
            .surfaceNormal = ToVec3(controller->surfaceInfo.surfaceNormal),
            .supportNormal = ToVec3(controller->supportNorm),
            .position = { position.x, position.y, position.z },
            .velocity = velocity,
            .deltaSeconds = delta,
        });
        const bool grounded = ground.contact == Core::Contact::Grounded;
        const auto observed = Observed(velocity, grounded);
        const auto physics = Settings::Physics();
        if (g_tumbleGuard) {
            Camera::EndTumbleShot();
            Camera::LeaveKnockdownCamera(player);
        }
        FollowSnow(player, controller, position, grounded, starting || (grounded && g_airVelocity.count > 0), delta,
            physics);
        bool flowFilled = false;
        if (physics.flow && g_flowLast && position.GetDistance(*g_flowLast) <= kTeleportUnits) {
            flowFilled = Core::FlowDescend(g_flow, (g_flowLast->z - position.z) / kUnitsPerMetre, FlowSettingsOf(physics));
            if (flowFilled) {
                spdlog::info("flow: full after {:.0f}m down; takes {:.0f}m of fall", physics.flowFillMetres,
                    g_flow.reserve);
                Sounds::PlayFlow(player);
            }
        }
        g_flowLast = position;
        float landingSpeed = 0.0F;

        if (starting) {
            spdlog::info("skiing active");
            g_debug.ski = {};
            g_debug.ski.velocity = observed;  // keep the momentum we arrived with
            g_airVelocity.Reset();
        } else if (!grounded) {
            g_airVelocity.Add(velocity);
        } else if (g_airVelocity.count > 0) {
            const auto  support = ToVec3(controller->supportNorm);
            const auto  normal = support.z > 0.1F ? support : ground.normal;
            const auto  touchdown = g_airVelocity.Hardest(normal);
            const auto  landed = Core::LandingVelocity(touchdown, normal);
            const auto& flying = g_debug.ski.velocity;
            if (Core::Length(landed) > Core::Length(flying)) {
                const float kept = std::clamp(physics.landingSpeedKept, 0.0F, 1.0F);
                g_debug.ski.velocity = { flying.x + (landed.x - flying.x) * kept, flying.y + (landed.y - flying.y) * kept };
            }
            landingSpeed = std::max(0.0F, Core::SpeedInto(touchdown, normal));
            LandingDamage(player, touchdown, g_airVelocity.takeoff, normal, g_snow.powder, physics);
            g_airVelocity.Reset();
        }

        Core::SkiInput input;
        input.deltaSeconds = delta;
        input.grounded = grounded;
        input.hasNormal = ground.hasNormal;
        input.normal = ground.normal;
        input.yaw = player->data.angle.z;
        input.powder = g_snow.powder;
        Camera::Observe(player);
        ReadControls(input, Camera::ViewYaw(player), Camera::ChaseYaw(player), delta, physics);
        input.hasObserved = !starting;
        input.observed = observed;
        auto*      values = player->AsActorValueOwner();
        const bool staminaCosts = !RE::PlayerCharacter::IsGodMode() &&
                                  (physics.staminaPerCarveSecond > 0.0F || physics.staminaPerStride > 0.0F);
        input.tired = staminaCosts && values->GetActorValue(RE::ActorValue::kStamina) < 1.0F;
        if (input.tired != g_tired) {
            g_tired = input.tired;
            spdlog::info(g_tired ? "out of stamina: edges limited, no skating" : "stamina back");
        }
        const auto step = Core::StepSki(g_debug.ski, input, physics);
        g_sinceStride = step.skated ? 0.0F : g_sinceStride + delta;
        if (const float cost = staminaCosts ? Core::StaminaCost(step, grounded, delta, physics) : 0.0F; cost > 0.0F) {
            values->DamageActorValue(RE::ActorValue::kStamina, cost);
        }

        SetYaw(player, step.yaw);
        Skis::Sync(player, skis);
        Pose::Prepare(player);
        const float turnBlend = starting ? 1.0F : 1.0F - std::exp(-std::max(0.0F, delta) / kTurnSmoothingSeconds);
        g_turnAcceleration += (step.turnAcceleration - g_turnAcceleration) * turnBlend;
        const float slipSpeed = std::clamp((Core::Length(step.velocity) - kSlipStartSpeed) / (kSlipFullSpeed - kSlipStartSpeed),
            0.0F, 1.0F);
        const float slipBlend = starting ? 1.0F : 1.0F - std::exp(-std::max(0.0F, delta) / kSlipSmoothingSeconds);
        g_slip += (step.slipDegrees * std::numbers::pi_v<float> / 180.0F * slipSpeed - g_slip) * slipBlend;
        if (grounded) {
            g_airPeak.reset();
        } else {
            g_airPeak = std::max(g_airPeak.value_or(position.z), position.z);
        }
        const float airDrop = g_airPeak ? (*g_airPeak - position.z) / kUnitsPerMetre : 0.0F;
        SetBody({
            .active = true,
            .tuck = step.tuck,
            .yaw = step.yaw,
            .edgeRadians = step.edgeDegrees * std::numbers::pi_v<float> / 180.0F,
            .turnAcceleration = g_turnAcceleration,
            .gravity = physics.gravity,
            .strideSide = step.strideSide,
            .strideSeconds = step.strideSeconds,
            .slipRadians = g_slip,
            .airDropMetres = airDrop,
            .groundNormal = grounded && ground.hasNormal ? ground.normal : Core::Vec3{ 0.0F, 0.0F, 1.0F },
            .skis = skis,
        });
        Camera::Apply(player, delta, step.velocity);
        SetCommand({ true, step.velocity });
        const auto hugged = HugGround(controller, velocity, ground, physics, delta);
        g_debug.hugging = hugged.has_value();
        if (hugged) {
            g_airVelocity.ReplaceLatest(*hugged);
        }
        const bool airborne =
            controller->surfaceInfo.supportedState.get() != RE::hkpSurfaceInfo::SupportedState::kSupported;
        g_crashSound = {};
        TrackImpacts(player, controller, position, velocity, airborne, hugged, BodyHeight(controller), physics, delta);
        Sounds::Update(player,
            {
                .deltaSeconds = delta,
                .grounded = grounded,
                .speed = Core::Length(step.velocity),
                .slipDegrees = step.slipDegrees,
                .turnAcceleration = g_turnAcceleration,
                .powder = physics.powder ? g_snow.powder : 0.0F,
                .strideStarted = step.skated,
                .strideSide = step.strideSide,
                .landingSpeed = landingSpeed,
                .crashSpeed = g_crashSound.speed,
            },
            Sounds::GroundOf(g_snow.reading.underfoot, g_snow.reading.land), g_crashSound.ground);
        CancelFall(controller, position);
        g_debug.heightCorrection = SyncHeight(player, controller, ground, physics, delta);
        SetOverlay({
            .active = true,
            .grounded = grounded,
            .yaw = step.yaw,
            .velocity = step.velocity,
            .downhill = { ground.normal.x, ground.normal.y },
            .slopeDegrees = std::acos(std::clamp(ground.normal.z, -1.0F, 1.0F)) * 180.0F / std::numbers::pi_v<float>,
            .edgeDegrees = step.edgeDegrees,
            .maxEdgeDegrees = physics.maxEdgeDegrees,
            .slipDegrees = step.slipDegrees,
            .gripLimit = step.gripLimit,
            .skidding = step.skidding,
            .parked = step.parked,
            .skating = g_sinceStride < kSkatingShownSeconds,
            .powder = physics.powder ? g_snow.powder : 0.0F,
            .material = Surface::MaterialName(g_snow.reading.land),
        });

        PublishFlow(physics, flowFilled, false);
        g_debug.active = true;
        g_debug.inactiveReason = "";
        g_debug.ground = ground;
        g_debug.step = step;
        g_debug.observedSpeed = Core::Length(observed);
    }

    void Reset()
    {
        g_tumbleGuard = false;
        g_tumbling = false;
        g_snow = {};
        g_flow = {};
        g_flowLast.reset();
        Sounds::MuteRagdollLoop(false);
    }

    void ApplyMovement(RE::PlayerCharacter* player, RE::NiPoint3& movement, RE::NiPoint3& rotation)
    {
        Command command;
        {
            std::scoped_lock lock(g_commandLock);
            command = g_command;
        }
        if (!command.active) {
            return;
        }
        const float yaw = player->data.angle.z;
        movement.x = Core::Dot(command.velocity, Core::Right(yaw)) * kMovementPerMeterPerSecond;
        movement.y = Core::Dot(command.velocity, Core::Forward(yaw)) * kMovementPerMeterPerSecond;
        rotation.z = 0.0F;
    }

    const Debug& GetDebug() { return g_debug; }

    OverlayData GetOverlay()
    {
        std::scoped_lock lock(g_overlayLock);
        return g_overlay;
    }

    FlowView GetFlow()
    {
        std::scoped_lock lock(g_flowLock);
        return g_flowView;
    }

    Body GetBody()
    {
        std::scoped_lock lock(g_overlayLock);
        return g_body;
    }
}
