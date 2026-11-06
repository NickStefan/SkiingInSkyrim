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
        // Steering with the right stick, the left stick's sideways push turns the camera this fast at full.
        constexpr float kLookDegreesPerSecond = 150.0F;

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

        Core::Vec2 FreshStick(float x, float y, float age)
        {
            return age >= 0.0F && age < kStaleStickSeconds ? Core::Vec2{ x, y } : Core::Vec2{};
        }

        // Edge: triggers (analog) or mouse buttons. Skid: its bindings. Aim: the steering stick (left, or right
        // if set) or W/A/S/D, relative to viewYaw (where the camera looks). Returns radians to turn the camera
        // this frame: steering with the right stick, the left stick looks around instead.
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
            if (keys.x != 0.0F || keys.y != 0.0F) {
                ski.aimYaw = viewYaw + std::atan2(keys.x, keys.y);
                ski.aimStrength = 1.0F;
            } else {
                ski.aimYaw = viewYaw + std::atan2(g_stick.x, g_stick.y);
                ski.aimStrength = Core::AimStrength(g_stick.x, g_stick.y, physics.aimDeadzone);
            }

            if (!rightSteers) {
                return 0.0F;
            }
            const float look = Core::AimStrength(left.x, 0.0F, physics.aimDeadzone);
            return std::copysign(look, left.x) * kLookDegreesPerSecond * std::numbers::pi_v<float> / 180.0F *
                   std::max(0.0F, delta);
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
            g_ground.Reset();
            g_skidFromToggle = true;
            g_stick = {};
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

        SetYaw(player, step.yaw);
        Camera::Apply(player, delta, step.velocity, lookTurn);
        SetCommand({ true, step.velocity });
        g_debug.hugging = HugGround(controller, velocity, ground, physics, delta);
        SoftenFall(controller, position, velocity, ground, physics);
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
