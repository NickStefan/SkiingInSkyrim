#include "plugin/Skiing.h"

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
        constexpr float kStickDeadzone = 0.05F;

        struct Command
        {
            bool       active{ false };
            Core::Vec2 velocity{};
        };

        std::mutex g_commandLock;
        Command    g_command;

        Core::GroundTracker g_ground;
        Debug               g_debug;

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

        // Whichever of keys or stick last asked for edge decides how the edge releases.
        bool g_edgeFromKeys = true;

        void ReadControls(Core::SkiInput& ski)
        {
            const auto input = Input::Get();
            const float keys = (input.right ? 1.0F : 0.0F) - (input.left ? 1.0F : 0.0F);
            const bool  stickFresh = input.stickAge >= 0.0F && input.stickAge < kStaleStickSeconds;
            float       edge = 0.0F;
            if (keys != 0.0F) {
                g_edgeFromKeys = true;
                edge = keys;
            } else if (stickFresh && std::fabs(input.stickX) > kStickDeadzone) {
                g_edgeFromKeys = false;
                edge = input.stickX;
            }
            ski.edgeTarget = edge;
            ski.edgeFromKeys = g_edgeFromKeys;
            ski.pivotTarget = input.pivotRight - input.pivotLeft;
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
            return;
        }

        RE::hkVector4 havokVelocity;
        controller->GetLinearVelocityImpl(havokVelocity);
        const auto velocity = ToVec3(havokVelocity);
        const auto position = player->GetPosition();
        const bool starting = !g_debug.active;
        if (starting) {
            g_ground.Reset();
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
        ReadControls(input);
        input.hasObserved = !starting;
        input.observed = observed;
        const auto step = Core::StepSki(g_debug.ski, input, Settings::Physics());

        SetYaw(player, step.yaw);
        SetCommand({ true, step.velocity });

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
}
