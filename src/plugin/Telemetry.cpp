#include "plugin/Telemetry.h"

#include "core/GroundEstimate.h"
#include "core/Slope.h"
#include "plugin/Camera.h"
#include "plugin/Havok.h"
#include "plugin/Input.h"
#include "plugin/Movement.h"
#include "plugin/Settings.h"
#include "plugin/Skiing.h"
#include "plugin/SkiingState.h"
#include "plugin/Tdm.h"

#include <deque>
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

        constexpr float kUnitsPerMetre = 70.0F;

        // One line per spell off the ground (engine support lost until regained) while skiing, to see how big
        // each hop over a crest really is.
        struct AirLog
        {
            bool       inAir{ false };
            float      seconds{ 0.0F };
            Core::Vec3 start{};
            Core::Vec3 plane{ 0.0F, 0.0F, 1.0F };  // the slope taken off from
            float      takeoffSpeed{ 0.0F };
            float      takeoffVertical{ 0.0F };
            float      takeoffSlope{ 0.0F };
            float      peak{ 0.0F };  // metres above the slope taken off from
            float      lastVertical{ 0.0F };
            float      fallMetres{ -1.0F };  // what the game counts as the fall, last frame in the air
            int        frames{ 0 };
            int        hugFrames{ 0 };
            bool       jumped{ false };

            void Update(bool supported, bool jumping, const Core::Vec3& position, const Core::Vec3& velocity,
                const Skiing::Debug& ski, float delta)
            {
                if (!inAir) {
                    if (supported) {
                        return;
                    }
                    *this = {};
                    inAir = true;
                    start = position;
                    plane = ski.ground.hasNormal ? ski.ground.normal : Core::Vec3{ 0.0F, 0.0F, 1.0F };
                    takeoffSpeed = Core::HorizontalLength(velocity);
                    takeoffVertical = velocity.z;
                    takeoffSlope = std::acos(std::clamp(plane.z, -1.0F, 1.0F)) * kDegrees;
                }
                if (!supported) {
                    seconds += delta;
                    ++frames;
                    hugFrames += ski.hugging ? 1 : 0;
                    jumped = jumped || jumping;
                    lastVertical = velocity.z;
                    fallMetres = ski.fallMetres;
                    const Core::Vec3 offset{ position.x - start.x, position.y - start.y, position.z - start.z };
                    peak = std::max(peak, (offset.x * plane.x + offset.y * plane.y + offset.z * plane.z) / kUnitsPerMetre);
                    return;
                }
                inAir = false;
                spdlog::info("air {:.2f}s{}: takeoff speed={:.1f}m/s vz={:+.1f} slope={:.0f} | peak above that slope={:.2f}m "
                             "| landing vz={:+.1f} slope={:.0f} | hug {}/{} frames | fall counted={}",
                    seconds, jumped ? " (jump)" : "", takeoffSpeed, takeoffVertical, takeoffSlope, peak, lastVertical,
                    std::acos(std::clamp(ski.ground.normal.z, -1.0F, 1.0F)) * kDegrees, hugFrames, frames, fallMetres >= 0.0F ? fmt::format("{:.1f}m", fallMetres) : std::string("?"));
            }
        };

        // Frame-to-frame shake of a tracked point: how much its velocity changes from one frame to the next
        // (m/s), as RMS and worst over a sample window. Smooth skiing changes it by about acceleration * dt
        // (well under 0.5 m/s); jitter shows up far above that.
        // Frames shorter than this (seconds) turn a normal movement into an absurd speed; shake skips them.
        constexpr float kShortestFrame = 0.002F;

        struct Shake
        {
            std::optional<Core::Vec3> last;
            std::optional<Core::Vec3> lastVelocity;
            float                     sumSquares{ 0.0F };
            float                     worst{ 0.0F };
            int                       count{ 0 };
            bool                      verticalOnly{ false };

            void Add(const Core::Vec3& point, float delta)
            {
                if (delta < kShortestFrame) {
                    Lose();
                    return;
                }
                if (last) {
                    Core::Vec3 velocity{ (point.x - last->x) / delta / kUnitsPerMetre,
                        (point.y - last->y) / delta / kUnitsPerMetre, (point.z - last->z) / delta / kUnitsPerMetre };
                    if (verticalOnly) {
                        velocity.x = velocity.y = 0.0F;
                    }
                    if (lastVelocity) {
                        const float change = Core::Length({ velocity.x - lastVelocity->x, velocity.y - lastVelocity->y,
                            velocity.z - lastVelocity->z });
                        sumSquares += change * change;
                        worst = std::max(worst, change);
                        ++count;
                    }
                    lastVelocity = velocity;
                }
                last = point;
            }

            void Lose() { last.reset(), lastVelocity.reset(); }

            std::string Take()
            {
                const auto text = fmt::format("{:.2f}/{:.2f}", count ? std::sqrt(sumSquares / static_cast<float>(count)) : 0.0F,
                    worst);
                sumSquares = worst = 0.0F;
                count = 0;
                return text;
            }
        };

        Core::Vec3 FromNi(const RE::NiPoint3& point) { return { point.x, point.y, point.z }; }

        // Per-frame trace while the character shakes vertically: the game's position next to the Havok body's,
        // the controller's state, and how many times the movement was applied, to see where the bounce comes
        // from. Starts when the vertical velocity changes by more than kTraceTrigger (m/s) in a frame, includes
        // the kTraceBefore frames before, and runs until kTraceLinger seconds after the last such frame.
        constexpr float       kTraceTrigger = 3.0F;
        constexpr float       kTraceLinger = 0.5F;
        constexpr std::size_t kTraceBefore = 10;
        constexpr int         kTraceMaxLines = 900;  // per skiing session

        struct FrameTrace
        {
            std::deque<std::string> before;
            float                   seconds{ 0.0F };
            float                   untilSeconds{ -1.0F };
            int                     lines{ 0 };
            std::optional<float>    lastZ;
            std::optional<float>    lastHavokZ;
            std::optional<float>    lastVerticalSpeed;

            void Reset() { *this = {}; }

            void Update(RE::PlayerCharacter* player, RE::bhkCharacterController* controller, float delta,
                const Skiing::Debug& ski)
            {
                seconds += delta;
                const auto  at = player->GetPosition();
                RE::hkVector4 havokPosition;
                controller->GetPositionImpl(havokPosition, false);
                const float havokZ = havokPosition.quad.m128_f32[2] / RE::bhkWorld::GetWorldScale();
                RE::hkVector4 havokVelocity;
                controller->GetLinearVelocityImpl(havokVelocity);
                const auto velocity = ToVec3(havokVelocity);
                const auto calls = Movement::TakeCalls();
                const auto move = Movement::Last();

                const float dz = lastZ ? (at.z - *lastZ) / kUnitsPerMetre : 0.0F;
                const float havokDz = lastHavokZ ? (havokZ - *lastHavokZ) / kUnitsPerMetre : 0.0F;
                const float verticalSpeed = delta > 0.0F ? dz / delta : 0.0F;
                const float jolt = lastVerticalSpeed ? std::fabs(verticalSpeed - *lastVerticalSpeed) : 0.0F;
                const bool  first = !lastZ;
                lastZ = at.z;
                lastHavokZ = havokZ;
                lastVerticalSpeed = verticalSpeed;
                if (first) {
                    return;
                }

                const auto& n = ski.ground.normal;
                const float slopeVz = n.z > 0.1F ? -(n.x * velocity.x + n.y * velocity.y) / n.z : 0.0F;
                auto line = fmt::format(
                    "trace t={:.3f} dt={:.4f} dz={:+.3f}m havok-dz={:+.3f}m z-havok={:+.1f} vz-frame={:+.2f} jolt={:.2f} "
                    "| havok-vel=({:+.2f},{:+.2f},{:+.2f}) slope-vz={:+.2f} ski-speed={:.2f} | state={} support={} "
                    "flags={:#x} normal-z={:.3f} support-z={:.3f} | moves={} move-dt={:.4f} move=({:+.2f},{:+.2f},{:+.2f}) hug={} "
                    "lowered={:.1f}",
                    seconds, delta, dz, havokDz, at.z - havokZ, verticalSpeed, jolt, velocity.x, velocity.y, velocity.z,
                    slopeVz, Core::Length(ski.ski.velocity), StateName(static_cast<std::uint32_t>(controller->context.currentState)),
                    SupportName(controller->surfaceInfo.supportedState.get()), controller->flags.underlying(),
                    controller->surfaceInfo.surfaceNormal.quad.m128_f32[2], controller->supportNorm.quad.m128_f32[2],
                    calls.count, calls.delta, move.movement.x, move.movement.y, move.movement.z, ski.hugging,
                    ski.heightCorrection);

                if (jolt > kTraceTrigger) {
                    if (untilSeconds < seconds && lines < kTraceMaxLines) {
                        spdlog::info("trace: shaking, {} frames before it follow", before.size());
                        for (const auto& earlier : before) {
                            spdlog::info("{}", earlier);
                            ++lines;
                        }
                    }
                    before.clear();
                    untilSeconds = seconds + kTraceLinger;
                }
                if (seconds <= untilSeconds) {
                    if (lines < kTraceMaxLines) {
                        spdlog::info("{}", line);
                        if (++lines == kTraceMaxLines) {
                            spdlog::info("trace: line limit reached for this session");
                        }
                    }
                    return;
                }
                before.push_back(std::move(line));
                if (before.size() > kTraceBefore) {
                    before.pop_front();
                }
            }
        };

        struct Sampler
        {
            AirLog        air;
            Shake         body;
            Shake         bodyVertical{ .verticalOnly = true };
            Shake         model;
            Shake         cameraShake;
            FrameTrace    trace;
            std::uint32_t lastState{ ~0U };
            int           stateFlips{ 0 };
            int           strides{ 0 };
            float         maxDelta{ 0.0F };
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
                if (ski.active) {
                    RE::hkVector4 frameVelocity;
                    controller->GetLinearVelocityImpl(frameVelocity);
                    const auto at = player->GetPosition();
                    air.Update(support == RE::hkpSurfaceInfo::SupportedState::kSupported,
                        controller->context.currentState == RE::hkpCharacterStateTypes::kJumping, { at.x, at.y, at.z },
                        ToVec3(frameVelocity), ski, delta);
                    TrackShake(player, controller, delta);
                    strides += ski.step.skated ? 1 : 0;
                    if (Settings::TraceShaking()) {
                        trace.Update(player, controller, delta, ski);
                    } else {
                        trace.Reset();
                    }
                } else {
                    air = {};
                    for (auto* shake : { &body, &bodyVertical, &model, &cameraShake }) {
                        shake->Lose();
                    }
                    trace.Reset();
                }
                const auto& estimate = ski.ground;
                const bool transition = ski.active && estimate.contact != lastContact;
                lastContact = estimate.contact;
                sinceSample += delta;
                if (!transition && sinceSample < kSampleInterval) {
                    return;
                }
                sinceSample = 0.0F;
                const int flips = std::exchange(supportFlips, 0);
                const int animFlips = std::exchange(stateFlips, 0);
                const float worstDelta = std::exchange(maxDelta, 0.0F);
                const int   strideCount = std::exchange(strides, 0);

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
                const auto* cameraRoot = CameraRoot();
                const auto  cameraAt = cameraRoot ? cameraRoot->world.translate : position;
                const float cameraDistance = Core::Length(FromNi(cameraAt - position)) / kUnitsPerMetre;
                const float cameraUp = (cameraAt.z - position.z) / kUnitsPerMetre;
                const char keys[] = { raw.forward ? 'W' : '-', raw.left ? 'A' : '-', raw.back ? 'S' : '-',
                    raw.right ? 'D' : '-', '\0' };

                spdlog::info(
                    "telemetry{} ski={} ground={} hug={} unsup={:.2f}s src={} wall={} smooth-slope={:.1f} smooth-fall-line={:+.0f} "
                    "| ski-vel=({:+.2f},{:+.2f}) ski-speed={:.2f} obs-speed={:.2f} blocked={} edge={:+.0f} pivot={:+.0f}{} "
                    "slip={:+.0f} grip={:.1f} skid={} triggers=({:.2f},{:.2f}) mouse={}{} "
                    "| cam={} cam-yaw={:.0f} look={:+.0f} "
                    "| state={} support={} flips={} slope={:.1f} fall-line={:+.0f} "
                    "normal=({:+.3f},{:+.3f},{:+.3f}) supportNorm=({:+.3f},{:+.3f},{:+.3f}) "
                    "vel=({:+.2f},{:+.2f},{:+.2f})m/s ground-speed={:.2f}m/s move=({:+.2f},{:+.2f},{:+.2f}) yaw={:.0f} "
                    "keys={} keyAge={:.2f} stick=({:+.2f},{:+.2f}) stickAge={:.2f} tdm={} "
                    "pos=({:.0f},{:.0f},{:.0f}) dt={:.4f} tuck={:.2f} pump={:.2f} strides={} "
                    "| shake rms/max m/s: body={} body-z={} model={} cam={} | cam-dist={:.2f}m cam-up={:+.2f}m "
                    "state-flips={} dt-max={:.4f}",
                    transition ? "*" : "", ski.active ? "active" : ski.inactiveReason, ContactName(estimate.contact),
                    ski.hugging,
                    estimate.unsupportedSeconds, SourceName(estimate.source), estimate.wallContact, smoothed.angleDegrees,
                    smoothed.fallLineOffsetDegrees,
                    ski.ski.velocity.x, ski.ski.velocity.y, Core::Length(ski.ski.velocity), ski.observedSpeed,
                    ski.step.blocked, ski.step.edgeDegrees, ski.step.pivotDegrees,
                    raw.skid ? "(skid)" : ski.step.parked ? "(stopped)" : "",
                    ski.step.slipDegrees, ski.step.gripLimit, ski.step.skidding, raw.triggerLeft, raw.triggerRight,
                    raw.mouseLeft ? 'L' : '-', raw.mouseRight ? 'R' : '-',
                    camera.status, camera.yawDegrees, camera.lookDegrees,
                    StateName(state), SupportName(support), flips, slope.angleDegrees, slope.fallLineOffsetDegrees,
                    normal.x, normal.y, normal.z, supportNormal.x, supportNormal.y, supportNormal.z,
                    velocity.x, velocity.y, velocity.z, Core::HorizontalLength(velocity),
                    move.movement.x, move.movement.y, move.movement.z, yaw * kDegrees,
                    keys, raw.keyAge, raw.stickX, raw.stickY, raw.stickAge,
                    Tdm::Status(), position.x, position.y, position.z, delta, ski.step.tuck, ski.step.pump, strideCount, body.Take(),
                    bodyVertical.Take(), model.Take(), cameraShake.Take(), cameraDistance, cameraUp, animFlips, worstDelta);
            }

            static RE::NiNode* CameraRoot()
            {
                auto* camera = RE::PlayerCamera::GetSingleton();
                return camera ? camera->cameraRoot.get() : nullptr;
            }

            // Every frame while skiing: where the character, its 3D model and the camera are.
            void TrackShake(RE::PlayerCharacter* player, RE::bhkCharacterController* controller, float delta)
            {
                const auto position = FromNi(player->GetPosition());
                body.Add(position, delta);
                bodyVertical.Add(position, delta);
                if (const auto* root = player->Get3D()) {
                    model.Add(FromNi(root->world.translate), delta);
                } else {
                    model.Lose();
                }
                if (const auto* root = CameraRoot()) {
                    cameraShake.Add(FromNi(root->world.translate), delta);
                } else {
                    cameraShake.Lose();
                }
                const auto state = static_cast<std::uint32_t>(controller->context.currentState);
                if (lastState != ~0U && state != lastState) {
                    ++stateFlips;
                }
                lastState = state;
                maxDelta = std::max(maxDelta, delta);
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
