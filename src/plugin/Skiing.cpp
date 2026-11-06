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

        // A stick that hasn't reported for this long is ignored.
        constexpr float kStaleStickSeconds = 0.5F;
        constexpr float kStickSmoothingSeconds = 0.08F;
        constexpr float kTriggerDeadzone = 0.02F;

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

        // The left stick, smoothed so thumb twitches don't become ski twitches.
        Core::Vec2 g_stick{};

        // Edge: triggers (analog) or mouse buttons. Skid: its bindings. Aim: the left stick or W/A/S/D,
        // relative to viewYaw (where the camera looks).
        void ReadControls(Core::SkiInput& ski, float viewYaw, float delta, const Core::SkiSettings& physics)
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

            const bool       stickFresh = input.stickAge >= 0.0F && input.stickAge < kStaleStickSeconds;
            const Core::Vec2 stick = stickFresh ? Core::Vec2{ input.stickX, input.stickY } : Core::Vec2{};
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
        ReadControls(input, Camera::ViewYaw(player), delta, physics);
        input.hasObserved = !starting;
        input.observed = observed;
        const auto step = Core::StepSki(g_debug.ski, input, physics);

        SetYaw(player, step.yaw);
        Camera::Apply(player, delta, step.velocity);
        SetCommand({ true, step.velocity });
        g_debug.hugging = HugGround(controller, velocity, ground, physics, delta);
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
