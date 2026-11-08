#include "plugin/Skiing.h"

#include "plugin/Camera.h"
#include "plugin/Havok.h"
#include "plugin/Input.h"
#include "plugin/Pose.h"
#include "plugin/Settings.h"
#include "plugin/Skis.h"
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
            const auto* state = player->AsActorState();
            if (state->GetLifeState() != RE::ACTOR_LIFE_STATE::kAlive) {
                return "not alive";
            }
            if (player->IsInWater() || state->IsSwimming()) {
                return "water";
            }
            if (player->IsOnMount()) {
                return "mounted";
            }
            if (player->IsInRagdollState()) {
                return "ragdoll";
            }
            if (player->IsInKillMove()) {
                return "kill move";
            }
            if (state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
                return "furniture";
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

        // Briefly unsupported after skiing over a crest (not a jump): keep the skier on the slope.
        bool HugGround(RE::bhkCharacterController* controller, const Core::Vec3& velocity,
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
                return false;
            }
            const float vertical = Core::GroundHugVerticalSpeed(velocity, ground.normal, delta, physics);
            controller->SetLinearVelocityImpl(RE::hkVector4(velocity.x, velocity.y, vertical, 0.0F));
            return true;
        }

        // Which units the controller's fall start height is in, once seen: 1 for game units, the Havok world
        // scale for Havok units.
        std::optional<float> g_fallUnits;

        // The game's own fall start this time off the ground, and what we last replaced it with.
        std::optional<float> g_fallStart;
        std::optional<float> g_fallWritten;

        // The landing is looked for along the path of free fall, a step at a time, from a little above the feet
        // so the first step doesn't start inside the slope just left.
        constexpr float kLandingStepSeconds = 0.25F;
        constexpr int   kLandingSteps = 8;
        constexpr float kLandingProbeLift = 20.0F;  // game units

        // The normal of the ground this fall will land on, if it's within reach.
        std::optional<Core::Vec3> PredictLanding(RE::PlayerCharacter* player, RE::bhkCharacterController* controller,
            const RE::NiPoint3& position, const Core::Vec3& velocity, float gravity)
        {
            auto* cell = player->GetParentCell();
            auto* world = cell ? cell->GetbhkWorld() : nullptr;
            if (!world) {
                return std::nullopt;
            }
            RE::CFilter own;
            controller->GetCollisionFilterInfo(own);
            const std::uint32_t filter =
                (own.GetSystemGroup() << 16) | static_cast<std::uint32_t>(RE::COL_LAYER::kCharController);
            const float scale = RE::bhkWorld::GetWorldScale();
            const auto  havokAt = [&](float t) {
                const float rise = (velocity.z * t - 0.5F * gravity * t * t) * kUnitsPerMetre;
                return RE::hkVector4((position.x + velocity.x * t * kUnitsPerMetre) * scale,
                    (position.y + velocity.y * t * kUnitsPerMetre) * scale, (position.z + kLandingProbeLift + rise) * scale,
                    0.0F);
            };
            auto from = havokAt(0.0F);
            for (int i = 1; i <= kLandingSteps; ++i) {
                const auto      to = havokAt(static_cast<float>(i) * kLandingStepSeconds);
                RE::bhkPickData pick;
                pick.rayInput.from = from;
                pick.rayInput.to = to;
                pick.rayInput.filterInfo.filter = filter;
                world->PickObject(pick);
                if (pick.rayOutput.HasHit()) {
                    return ToVec3(pick.rayOutput.normal);
                }
                from = to;
            }
            return std::nullopt;
        }

        // Skyrim's fall damage counts the height dropped since leaving the ground. On skis what hurts is how
        // hard you meet the ground, so while off the ground the fall is restarted no higher than would land as
        // hard as meeting the predicted landing slope (or the slope just left, when it's out of reach) at the
        // current velocity. Never more than the game's own.
        void SoftenFall(RE::PlayerCharacter* player, RE::bhkCharacterController* controller, const RE::NiPoint3& position,
            const Core::Vec3& velocity, const Core::GroundEstimate& ground, const Core::SkiSettings& physics)
        {
            g_debug.fallMetres = -1.0F;
            g_debug.landingSlopeDegrees = -1.0F;
            if (controller->surfaceInfo.supportedState.get() == RE::hkpSurfaceInfo::SupportedState::kSupported) {
                g_fallStart.reset();
                g_fallWritten.reset();
                return;
            }
            if (!g_fallUnits) {
                // Decide from the height the game recorded at takeoff, once it's far enough from 0 to tell.
                const float start = controller->fallStartHeight;
                const float havok = RE::bhkWorld::GetWorldScale();
                if (std::fabs(position.z) < 500.0F || start == 0.0F) {
                    return;
                }
                g_fallUnits = std::fabs(start - position.z) <= std::fabs(start - position.z * havok) ? 1.0F : havok;
                spdlog::info("fall start height is in {} units (start={:.1f}, z={:.1f})",
                    *g_fallUnits == 1.0F ? "game" : "Havok", start, position.z);
            }
            if (!g_fallWritten || controller->fallStartHeight != *g_fallWritten) {
                g_fallStart = controller->fallStartHeight;
            }
            const auto landing = PredictLanding(player, controller, position, velocity, physics.gravity);
            if (landing) {
                g_debug.landingSlopeDegrees =
                    std::acos(std::clamp(landing->z, -1.0F, 1.0F)) * 180.0F / std::numbers::pi_v<float>;
            }
            const auto  normal = landing ? *landing : ground.hasNormal ? ground.normal : Core::Vec3{ 0.0F, 0.0F, 1.0F };
            const float metres = Core::LandingHeight(velocity, normal, physics);
            const float soft = (position.z + metres * kUnitsPerMetre) * *g_fallUnits;
            controller->fallStartHeight = std::min(*g_fallStart, soft);
            g_fallWritten = controller->fallStartHeight;
            g_debug.fallMetres = (controller->fallStartHeight / *g_fallUnits - position.z) / kUnitsPerMetre;
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
                        if (!g_height.offset || std::fabs(*g_height.offset - learned) > 2.0F) {
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
        if (const char* reason = InactiveReason(player, controller, skis)) {
            if (g_debug.active) {
                spdlog::info("skiing inactive: {}", reason);
            }
            g_debug.active = false;
            g_debug.inactiveReason = reason;
            SetCommand({});
            SetOverlay({ .active = false, .inactiveReason = reason });
            Camera::Reset();
            // The stance blends out from the last pose, so it keeps where the skis pointed.
            auto body = GetBody();
            body.active = false;
            body.airDropMetres = 0.0F;
            SetBody(body);
            Skis::Sync(player, nullptr);
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
            g_fallStart.reset();
            g_fallWritten.reset();
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

        if (starting) {
            spdlog::info("skiing active");
            g_debug.ski = {};
            g_debug.ski.velocity = observed;  // keep the momentum we arrived with
        }

        Core::SkiInput input;
        input.deltaSeconds = delta;
        input.grounded = grounded;
        input.hasNormal = ground.hasNormal;
        input.normal = ground.normal;
        input.yaw = player->data.angle.z;
        const auto physics = Settings::Physics();
        Camera::Observe(player);
        ReadControls(input, Camera::ViewYaw(player), Camera::ChaseYaw(player), delta, physics);
        input.hasObserved = !starting;
        input.observed = observed;
        auto*      values = player->AsActorValueOwner();
        const bool staminaCosts = physics.staminaPerCarveSecond > 0.0F || physics.staminaPerStride > 0.0F;
        input.tired = staminaCosts && values->GetActorValue(RE::ActorValue::kStamina) < 1.0F;
        if (input.tired != g_tired) {
            g_tired = input.tired;
            spdlog::info(g_tired ? "out of stamina: edges limited, no skating" : "stamina back");
        }
        const auto step = Core::StepSki(g_debug.ski, input, physics);
        g_sinceStride = step.skated ? 0.0F : g_sinceStride + delta;
        if (const float cost = Core::StaminaCost(step, grounded, delta, physics); cost > 0.0F) {
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
        g_debug.hugging = HugGround(controller, velocity, ground, physics, delta);
        SoftenFall(player, controller, position, velocity, ground, physics);
        g_debug.heightCorrection = SyncHeight(player, controller, ground, physics, delta);
        SetOverlay({
            .active = true,
            .inactiveReason = "",
            .grounded = grounded,
            .yaw = step.yaw,
            .velocity = step.velocity,
            .downhill = { ground.normal.x, ground.normal.y },
            .slopeDegrees = std::acos(std::clamp(ground.normal.z, -1.0F, 1.0F)) * 180.0F / std::numbers::pi_v<float>,
            .edgeDegrees = step.edgeDegrees,
            .maxEdgeDegrees = physics.maxEdgeDegrees,
            .pivotDegrees = step.pivotDegrees,
            .maxPivotDegrees = physics.pivotMaxDegrees,
            .slipDegrees = step.slipDegrees,
            .gripLimit = step.gripLimit,
            .skidding = step.skidding,
            .parked = step.parked,
            .tuck = step.tuck,
            .skating = g_sinceStride < kSkatingShownSeconds,
        });

        g_debug.active = true;
        g_debug.inactiveReason = "";
        g_debug.ground = ground;
        g_debug.step = step;
        g_debug.observedSpeed = Core::Length(observed);
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

    Body GetBody()
    {
        std::scoped_lock lock(g_overlayLock);
        return g_body;
    }
}
