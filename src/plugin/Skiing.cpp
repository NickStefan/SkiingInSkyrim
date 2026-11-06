#include "plugin/Skiing.h"

#include "plugin/Camera.h"
#include "plugin/Havok.h"
#include "plugin/Input.h"
#include "plugin/Settings.h"
#include "plugin/SkiingState.h"
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
        // Steering with the right stick or mouse, the left stick's sideways push or A/D turn the camera this
        // fast at full.
        constexpr float kLookDegreesPerSecond = 150.0F;
        // Held movement keys push the aim from nothing to full over this long (with the stick's squared
        // response), so a tap nudges and a hold turns; starting again whenever the keys held change.
        constexpr float kKeyAimBuildSeconds = 0.6F;
        // Mouse steering beyond what the skis can turn in a frame carries over, fading with this time
        // constant, so a quick flick still turns but doesn't keep turning.
        constexpr float kMouseCarrySeconds = 0.1F;

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

        void SetOverlay(const OverlayData& overlay)
        {
            std::scoped_lock lock(g_overlayLock);
            g_overlay = overlay;
        }

        void SetCommand(const Command& command)
        {
            std::scoped_lock lock(g_commandLock);
            g_command = command;
        }

        const char* InactiveReason(RE::PlayerCharacter* player, RE::bhkCharacterController* controller)
        {
            if (!IsSkiingEnabled()) {
                return "off";
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
        bool g_skidFromToggle = false;

        // The steering stick, smoothed so thumb twitches don't become ski twitches.
        Core::Vec2 g_stick{};

        Core::Vec2 g_heldKeys{};
        float      g_keyHold = 0.0F;
        float      g_mouseTurn = 0.0F;  // radians of mouse steering not yet applied

        Core::Vec2 FreshStick(float x, float y, float age)
        {
            return age >= 0.0F && age < kStaleStickSeconds ? Core::Vec2{ x, y } : Core::Vec2{};
        }

        // Edge: triggers (analog) or mouse buttons. Skid: its bindings. Aim: the steering stick (left, or right
        // if set) or W/A/S/D, relative to viewYaw (where the camera looks). Steering with the right stick, the
        // mouse turns the skis directly, W tucks, S brakes, and the left stick and A/D look around instead.
        // Returns radians to turn the camera this frame.
        float ReadControls(Core::SkiInput& ski, float viewYaw, float delta, const Core::SkiSettings& physics)
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

            // Skid held as skiing starts is usually part of the toggle chord (it can share a button), so it
            // only counts once released and pressed again.
            if (!input.skid) {
                g_skidFromToggle = false;
            }
            ski.skidHeld = input.skid && !g_skidFromToggle;

            const auto       left = FreshStick(input.stickX, input.stickY, input.stickAge);
            const bool       rightSteers = Settings::AimWithRightStick();
            const Core::Vec2 stick = rightSteers ? FreshStick(input.rightStickX, input.rightStickY, input.rightStickAge) : left;
            const float      blend = 1.0F - std::exp(-std::max(0.0F, delta) / kStickSmoothingSeconds);
            g_stick = { g_stick.x + (stick.x - g_stick.x) * blend, g_stick.y + (stick.y - g_stick.y) * blend };

            const Core::Vec2 keys{ (input.right ? 1.0F : 0.0F) - (input.left ? 1.0F : 0.0F),
                (input.forward ? 1.0F : 0.0F) - (input.back ? 1.0F : 0.0F) };
            g_keyHold = keys.x != g_heldKeys.x || keys.y != g_heldKeys.y ? 0.0F : g_keyHold + std::max(0.0F, delta);
            g_heldKeys = keys;
            const float keyBuild = std::min(1.0F, g_keyHold / kKeyAimBuildSeconds);
            const float keyStrength = keyBuild * keyBuild;

            constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
            const int       mouseX = Input::TakeMouseX();
            if (!rightSteers) {
                g_mouseTurn = 0.0F;
                if (keys.x != 0.0F || keys.y != 0.0F) {
                    ski.aimYaw = viewYaw + std::atan2(keys.x, keys.y);
                    ski.aimStrength = keyStrength;
                } else {
                    ski.aimYaw = viewYaw + std::atan2(g_stick.x, g_stick.y);
                    ski.aimStrength = Core::AimStrength(g_stick.x, g_stick.y, physics.aimDeadzone);
                }
                return 0.0F;
            }

            g_mouseTurn += static_cast<float>(mouseX) * Settings::MouseSteerSensitivity() * kRadians;
            const float maxTurn = physics.steerDegreesPerSecond * kRadians * std::max(0.0F, delta);
            ski.yawTurn = std::clamp(g_mouseTurn, -maxTurn, maxTurn);
            g_mouseTurn = (g_mouseTurn - ski.yawTurn) * std::exp(-std::max(0.0F, delta) / kMouseCarrySeconds);
            if (keys.y > 0.0F) {
                ski.aimYaw = ski.yaw;
                ski.aimStrength = keyStrength;
            } else if (keys.y < 0.0F) {
                ski.aimYaw = viewYaw + std::numbers::pi_v<float>;
                ski.aimStrength = keyStrength;
            } else {
                ski.aimYaw = viewYaw + std::atan2(g_stick.x, g_stick.y);
                ski.aimStrength = Core::AimStrength(g_stick.x, g_stick.y, physics.aimDeadzone);
            }
            const float look = keys.x != 0.0F ? keys.x : std::copysign(Core::AimStrength(left.x, 0.0F, physics.aimDeadzone), left.x);
            return look * kLookDegreesPerSecond * kRadians * std::max(0.0F, delta);
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

        // Skyrim's fall damage counts the height dropped since leaving the ground. On skis what hurts is how
        // hard you meet the slope, so while off the ground the fall is restarted no higher than would land
        // as hard as meeting the slope just left at the current velocity. Never more than the game's own.
        void SoftenFall(RE::bhkCharacterController* controller, const RE::NiPoint3& position, const Core::Vec3& velocity,
            const Core::GroundEstimate& ground, const Core::SkiSettings& physics)
        {
            g_debug.fallMetres = -1.0F;
            if (controller->surfaceInfo.supportedState.get() == RE::hkpSurfaceInfo::SupportedState::kSupported) {
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
            const auto  normal = ground.hasNormal ? ground.normal : Core::Vec3{ 0.0F, 0.0F, 1.0F };
            const float metres = Core::LandingHeight(velocity, normal, physics);
            const float soft = (position.z + metres * kUnitsPerMetre) * *g_fallUnits;
            controller->fallStartHeight = std::min(controller->fallStartHeight, soft);
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
        if (const char* reason = InactiveReason(player, controller)) {
            if (g_debug.active) {
                spdlog::info("skiing inactive: {}", reason);
            }
            g_debug.active = false;
            g_debug.inactiveReason = reason;
            SetCommand({});
            SetOverlay({ .active = false, .inactiveReason = reason });
            Camera::Reset();
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
            g_skidFromToggle = true;
            g_stick = {};
            g_mouseTurn = 0.0F;
            Input::TakeMouseX();
            g_height.agreeing = 0;
            g_sinceStride = kSkatingShownSeconds;
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
        const float lookTurn = ReadControls(input, Camera::ViewYaw(player), delta, physics);
        input.hasObserved = !starting;
        input.observed = observed;
        const auto step = Core::StepSki(g_debug.ski, input, physics);
        g_sinceStride = step.skated ? 0.0F : g_sinceStride + delta;

        SetYaw(player, step.yaw);
        Camera::Apply(player, delta, step.velocity, lookTurn);
        SetCommand({ true, step.velocity });
        g_debug.hugging = HugGround(controller, velocity, ground, physics, delta);
        SoftenFall(controller, position, velocity, ground, physics);
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
}
