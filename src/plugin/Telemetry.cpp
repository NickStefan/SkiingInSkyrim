#include "plugin/Telemetry.h"

#include "core/GroundEstimate.h"
#include "core/Slope.h"
#include "plugin/Camera.h"
#include "plugin/Havok.h"
#include "plugin/Input.h"
#include "plugin/Movement.h"
#include "plugin/Skiing.h"
#include "plugin/SkiingState.h"
#include "plugin/Tdm.h"

#include <numbers>

namespace SkiingInSkyrim::Telemetry
{
    namespace
    {
        constexpr float kSampleInterval = 0.25F;
        constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;
        std::string_view StateName(std::uint32_t state) noexcept
        {
            using S = RE::hkpCharacterStateTypes;
            switch (state) {
            case S::kOnGround: return "OnGround";
            case S::kJumping:  return "Jumping";
            case S::kInAir:    return "InAir";
            case S::kClimbing: return "Climbing";
            case S::kFlying:   return "Flying";
            case S::kSwimming: return "Swimming";
            default:           return "Other";
            }
        }

        std::string_view SupportName(RE::hkpSurfaceInfo::SupportedState state) noexcept
        {
            using S = RE::hkpSurfaceInfo::SupportedState;
            switch (state) {
            case S::kUnsupported: return "Unsupported";
            case S::kSliding:     return "Sliding";
            case S::kSupported:   return "Supported";
            default:              return "Unknown";
            }
        }

        std::string_view ContactName(Core::Contact contact) noexcept
        {
            return contact == Core::Contact::Grounded ? "Grounded" : "Airborne";
        }

        std::string_view SourceName(Core::NormalSource source) noexcept
        {
            switch (source) {
            case Core::NormalSource::Support: return "support";
            case Core::NormalSource::Surface: return "surface";
            default:                          return "none";
            }
        }

        struct Sampler
        {
            float         sinceSample{ kSampleInterval };
            bool          wasEnabled{ false };
            Core::Contact lastContact{ Core::Contact::Grounded };
            std::uint32_t lastSupport{ ~0U };
            int           supportFlips{ 0 };

            void Update(RE::PlayerCharacter* player, float delta)
            {
                if (!IsSkiingEnabled()) {
                    wasEnabled = false;
                    return;
                }
                if (!wasEnabled) {
                    wasEnabled = true;
                    sinceSample = kSampleInterval;
                    lastSupport = ~0U;
                    supportFlips = 0;
                }

                auto* controller = player->GetCharController();
                if (!controller) {
                    return;
                }

                const auto support = controller->surfaceInfo.supportedState.get();
                if (lastSupport != ~0U && static_cast<std::uint32_t>(support) != lastSupport) {
                    ++supportFlips;
                }
                lastSupport = static_cast<std::uint32_t>(support);

                const auto& ski = Skiing::GetDebug();
                const auto& estimate = ski.ground;
                const bool transition = ski.active && estimate.contact != lastContact;
                lastContact = estimate.contact;
                sinceSample += delta;
                if (!transition && sinceSample < kSampleInterval) {
                    return;
                }
                sinceSample = 0.0F;
                const int flips = std::exchange(supportFlips, 0);

                RE::hkVector4 havokVelocity;
                controller->GetLinearVelocityImpl(havokVelocity);
                const auto velocity = ToVec3(havokVelocity);
                const auto normal = ToVec3(controller->surfaceInfo.surfaceNormal);
                const auto supportNormal = ToVec3(controller->supportNorm);
                const auto position = player->GetPosition();
                const auto state = static_cast<std::uint32_t>(controller->context.currentState);
                const float yaw = player->data.angle.z;
                const auto slope = Core::AnalyzeSlope(normal, yaw);
                const auto smoothed = Core::AnalyzeSlope(estimate.normal, yaw);
                const auto raw = Input::Get();
                const auto move = Movement::Last();
                const auto camera = Camera::GetInfo();
                const char keys[] = { raw.forward ? 'W' : '-', raw.left ? 'A' : '-', raw.back ? 'S' : '-',
                    raw.right ? 'D' : '-', '\0' };

                spdlog::info(
                    "telemetry{} ski={} ground={} unsup={:.2f}s src={} wall={} smooth-slope={:.1f} smooth-fall-line={:+.0f} "
                    "| ski-vel=({:+.2f},{:+.2f}) ski-speed={:.2f} obs-speed={:.2f} blocked={} edge={:+.0f} pivot={:+.0f}{} "
                    "slip={:+.0f} grip={:.1f} skid={} triggers=({:.2f},{:.2f}) mouse={}{} "
                    "| cam={} cam-yaw={:.0f} look={:+.0f} "
                    "| state={} support={} flips={} slope={:.1f} fall-line={:+.0f} "
                    "normal=({:+.3f},{:+.3f},{:+.3f}) supportNorm=({:+.3f},{:+.3f},{:+.3f}) "
                    "vel=({:+.2f},{:+.2f},{:+.2f})m/s ground-speed={:.2f}m/s move=({:+.2f},{:+.2f},{:+.2f}) yaw={:.0f} "
                    "keys={} keyAge={:.2f} stick=({:+.2f},{:+.2f}) stickAge={:.2f} tdm={} pos=({:.0f},{:.0f},{:.0f}) dt={:.4f}",
                    transition ? "*" : "", ski.active ? "active" : ski.inactiveReason, ContactName(estimate.contact),
                    estimate.unsupportedSeconds, SourceName(estimate.source), estimate.wallContact, smoothed.angleDegrees,
                    smoothed.fallLineOffsetDegrees,
                    ski.ski.velocity.x, ski.ski.velocity.y, Core::Length(ski.ski.velocity), ski.observedSpeed,
                    ski.step.blocked, ski.step.edgeDegrees, ski.step.pivotDegrees,
                    raw.skid ? "(skid)" : ski.step.parked ? "(side-slip)" : "",
                    ski.step.slipDegrees, ski.step.gripLimit, ski.step.skidding, raw.triggerLeft, raw.triggerRight,
                    raw.mouseLeft ? 'L' : '-', raw.mouseRight ? 'R' : '-',
                    camera.status, camera.yawDegrees, camera.lookDegrees,
                    StateName(state), SupportName(support), flips, slope.angleDegrees, slope.fallLineOffsetDegrees,
                    normal.x, normal.y, normal.z, supportNormal.x, supportNormal.y, supportNormal.z,
                    velocity.x, velocity.y, velocity.z, Core::HorizontalLength(velocity),
                    move.movement.x, move.movement.y, move.movement.z, yaw * kDegrees,
                    keys, raw.keyAge, raw.stickX, raw.stickY, raw.stickAge, Tdm::Status(), position.x, position.y,
                    position.z, delta);
            }
        };

        Sampler g_sampler;

        struct PlayerUpdate
        {
            static void thunk(RE::PlayerCharacter* player, float delta)
            {
                func(player, delta);
                Movement::Update();
                Tdm::Update(IsSkiingEnabled());
                Skiing::Update(player, delta);
                g_sampler.Update(player, delta);
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

    }

    void Install()
    {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PlayerCharacter[0] };
        PlayerUpdate::func = vtable.write_vfunc(0xAD, PlayerUpdate::thunk);
        spdlog::info("hooked PlayerCharacter::Update (0xAD)");
    }
}
