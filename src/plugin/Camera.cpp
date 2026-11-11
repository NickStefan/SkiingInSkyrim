#include "plugin/Camera.h"

#include "core/CameraFollow.h"
#include "core/TumbleCamera.h"
#include "plugin/Settings.h"

#include <mutex>
#include <numbers>

namespace SkiingInSkyrim::Camera
{
    namespace
    {
        constexpr float kDegrees = 180.0F / std::numbers::pi_v<float>;

        float WrapSigned(float angle) noexcept
        {
            constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;
            angle = std::fmod(angle + std::numbers::pi_v<float>, kTwoPi);
            if (angle < 0.0F) {
                angle += kTwoPi;
            }
            return angle - std::numbers::pi_v<float>;
        }

        // Main thread only, except the atomics.
        struct State
        {
            Core::CameraFollowState  follow;
            std::optional<float>     observed;
            std::atomic<const char*> status{ "idle" };
            std::atomic<bool>        smoothCam{ false };
            std::atomic<bool>        enhancedDeathCam{ false };
            std::atomic<bool>        enhancedDeathCamRagdoll{ false };
            std::atomic<float>       yawDegrees{ 0.0F };
            std::atomic<float>       lookDegrees{ 0.0F };
        };

        State g_state;

        void SetStatus(const char* status)
        {
            if (g_state.status.exchange(status) != status) {
                spdlog::info("chase camera: {}", status);
            }
        }

        // The third-person state if the camera is in it and lets the camera orbit the player.
        RE::ThirdPersonState* ThirdPerson(const char** whyNot)
        {
            auto* camera = RE::PlayerCamera::GetSingleton();
            if (!camera || !camera->IsCurrentState(RE::CameraState::kThirdPerson, RE::CameraState::kVRThirdPerson)) {
                *whyNot = "not in third person";
                return nullptr;
            }
            auto* state = static_cast<RE::ThirdPersonState*>(camera->currentState.get());
            if (!state || !state->freeRotationEnabled) {
                *whyNot = "camera free rotation off";
                return nullptr;
            }
            return state;
        }

        bool InKnockdownCamera(RE::PlayerCamera* camera)
        {
            return camera && camera->IsCurrentState(RE::CameraState::kBleedout, RE::CameraState::kVRBleedout);
        }

        const char* StateName(RE::CameraState state)
        {
            switch (state) {
            case RE::CameraState::kFirstPerson:  return "first person";
            case RE::CameraState::kAutoVanity:   return "auto vanity";
            case RE::CameraState::kVATS:         return "VATS";
            case RE::CameraState::kFree:         return "free";
            case RE::CameraState::kIronSights:   return "iron sights";
            case RE::CameraState::kFurniture:    return "furniture";
            case RE::CameraState::kPCTransition: return "transition";
            case RE::CameraState::kTween:        return "tween";
            case RE::CameraState::kAnimated:     return "animated";
            case RE::CameraState::kThirdPerson:  return "third person";
            case RE::CameraState::kMount:        return "mount";
            case RE::CameraState::kBleedout:     return "bleedout";
            case RE::CameraState::kDragon:       return "dragon";
            default:                             return "other";
            }
        }

        std::string ModuleOf(std::uintptr_t address)
        {
            HMODULE module = nullptr;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(address), &module)) {
                return "unknown";
            }
            char path[MAX_PATH]{};
            const auto length = GetModuleFileNameA(module, path, MAX_PATH);
            return std::filesystem::path(std::string_view(path, length)).filename().string();
        }

        // Enhanced Death Cam's ragdoll camera is on unless its INI turns it off.
        bool EnhancedDeathCamRagdollCamera()
        {
            const auto path = (std::filesystem::current_path() / "Data/SKSE/Plugins/po3_EnhancedDeathCamera.ini").string();
            char value[16]{};
            GetPrivateProfileStringA("Ragdoll Camera", "bEnable", "true", value, sizeof(value), path.c_str());
            const std::string_view enabled(value);
            return enabled != "false" && enabled != "0";
        }
    }

    void Connect()
    {
        const bool smoothCam = GetModuleHandleA("SmoothCam.dll") || GetModuleHandleA("SmoothCamAE.dll");
        g_state.smoothCam = smoothCam;
        spdlog::info("SmoothCam {}", smoothCam ? "installed" : "not installed");

        const bool deathCam = GetModuleHandleA("po3_EnhancedDeathCamera.dll") != nullptr;
        g_state.enhancedDeathCam = deathCam;
        if (deathCam) {
            const bool ragdoll = EnhancedDeathCamRagdollCamera();
            g_state.enhancedDeathCamRagdoll = ragdoll;
            spdlog::info("Enhanced Death Cam installed, ragdoll camera {}{}", ragdoll ? "on" : "off",
                ragdoll && !smoothCam ? ": it keeps third person in a knockdown, so there's no tumble shot" : "");
        } else {
            spdlog::info("Enhanced Death Cam not installed");
        }
    }

    void Observe(RE::PlayerCharacter* player)
    {
        const char* whyNot = "";
        const auto* state = ThirdPerson(&whyNot);
        g_state.observed = state ? std::optional{ player->data.angle.z + state->freeRotation.x } : std::nullopt;
    }

    float ViewYaw(RE::PlayerCharacter* player)
    {
        return g_state.observed ? *g_state.observed : player->data.angle.z;
    }

    float ChaseYaw(RE::PlayerCharacter* player)
    {
        return g_state.follow.started && g_state.observed ? g_state.follow.yaw : ViewYaw(player);
    }

    void Apply(RE::PlayerCharacter* player, float delta, Core::Vec2 velocity)
    {
        const auto  settings = Settings::Camera();
        const char* whyNot = "";
        auto*       state = ThirdPerson(&whyNot);
        if (!settings.enabled) {
            g_state.follow = {};
            SetStatus("off");
            return;
        }
        if (!state || !g_state.observed) {
            g_state.follow = {};
            SetStatus(state ? "waiting" : whyNot);
            return;
        }
        const float yaw = Core::UpdateCameraFollow(g_state.follow, settings, delta, *g_state.observed, velocity);
        state->freeRotation.x = WrapSigned(yaw - player->data.angle.z);
        g_state.yawDegrees = g_state.follow.yaw * kDegrees;
        g_state.lookDegrees = g_state.follow.look * kDegrees;
        SetStatus("following travel");
    }

    namespace
    {
        constexpr float kUnitsPerMetre = 70.0F;

        // The tumble shot: set on the main thread, shown by the hooks on the game's knockdown camera below.
        struct Shot
        {
            std::optional<RE::NiPoint3> start;  // the camera at the hit
            bool                        filming{ false };
            Core::TumbleCameraState     follow;
            Core::LookAngles            aim;
            bool                        aimed{ false };  // aim holds this tumble's last look
            float                       logSeconds{ 0.0F };
            bool                        transposed{ false };  // the game's rotation matrices are row-major
            bool                        checked{ false };
            int                         otherFrames{ 0 };      // ragdoll frames outside the knockdown camera
            int                         updates{ 0 };          // hook calls while filming
            int                         rotations{ 0 };
            int                         translations{ 0 };
        };
        std::mutex g_shotLock;
        Shot       g_shot;

        Core::Vec3 ToVec(const RE::NiPoint3& p) { return { p.x, p.y, p.z }; }

        // Where the shot looks: the pelvis, which tumbles less than the head.
        Core::Vec3 BodyPoint(RE::PlayerCharacter* player)
        {
            auto* root = player->Get3D(false);
            for (const char* name : { "NPC Pelvis [Pelv]", "NPC Root [Root]" }) {
                if (auto* node = root ? root->GetObjectByName(name) : nullptr) {
                    return ToVec(node->world.translate);
                }
            }
            return ToVec(player->GetPosition());
        }

        // How far from one point toward another (game units) is clear for the camera, if something's in the way:
        // what the game's own camera collides with, not counting the player.
        std::optional<float> ClearUnits(RE::PlayerCharacter* player, const Core::Vec3& from, const Core::Vec3& to)
        {
            auto* cell = player->GetParentCell();
            auto* world = cell ? cell->GetbhkWorld() : nullptr;
            if (!world) {
                return std::nullopt;
            }
            RE::CFilter own;
            if (auto* controller = player->GetCharController()) {
                controller->GetCollisionFilterInfo(own);
            }
            const float     scale = RE::bhkWorld::GetWorldScale();
            RE::bhkPickData pick;
            pick.rayInput.from = RE::hkVector4(from.x * scale, from.y * scale, from.z * scale, 0.0F);
            pick.rayInput.to = RE::hkVector4(to.x * scale, to.y * scale, to.z * scale, 0.0F);
            pick.rayInput.filterInfo.filter =
                (own.GetSystemGroup() << 16) | static_cast<std::uint32_t>(RE::COL_LAYER::kCameraSphere);
            world->PickObject(pick);
            if (!pick.rayOutput.HasHit()) {
                return std::nullopt;
            }
            return pick.rayOutput.hitFraction * Core::Length({ to.x - from.x, to.y - from.y, to.z - from.z });
        }

        // A camera rotation looking along these angles: its local +y forward, +z up.
        RE::NiMatrix3 Rotation(const Core::LookAngles& aim, bool transposed)
        {
            const float cy = std::cos(aim.yaw), sy = std::sin(aim.yaw);
            const float cp = std::cos(aim.pitch), sp = std::sin(aim.pitch);
            const RE::NiPoint3 right{ cy, -sy, 0.0F };
            const RE::NiPoint3 forward{ sy * cp, cy * cp, -sp };
            const RE::NiPoint3 up = right.Cross(forward);
            RE::NiMatrix3 m;
            const RE::NiPoint3* columns[3] = { &right, &forward, &up };
            for (int c = 0; c < 3; ++c) {
                const float v[3] = { columns[c]->x, columns[c]->y, columns[c]->z };
                for (int r = 0; r < 3; ++r) {
                    (transposed ? m.entry[c][r] : m.entry[r][c]) = v[r];
                }
            }
            return m;
        }

        float FacingYaw(const RE::NiMatrix3& m, bool transposed)
        {
            return transposed ? std::atan2(m.entry[1][0], m.entry[1][1]) : std::atan2(m.entry[0][1], m.entry[1][1]);
        }

        // The knockdown camera looks at the player: whichever reading of its matrix faces them is the convention.
        void CheckConvention(const RE::NiTransform& camera)
        {
            g_shot.checked = true;
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return;
            }
            const auto  toPlayer = player->GetPosition() - camera.translate;
            const float yaw = std::atan2(toPlayer.x, toPlayer.y);
            g_shot.transposed = std::fabs(WrapSigned(FacingYaw(camera.rotate, true) - yaw)) <
                                std::fabs(WrapSigned(FacingYaw(camera.rotate, false) - yaw));
            spdlog::info("tumble camera: rotation matrices read {}", g_shot.transposed ? "by rows" : "by columns");
        }

        // The knockdown camera's own update runs first (and Enhanced Death Cam's, if it hooks it), then the
        // tumble shot replaces where it put the camera.
        struct KnockdownUpdateHook
        {
            static void thunk(RE::BleedoutCameraState* state, RE::BSTSmartPointer<RE::TESCameraState>& next)
            {
                func(state, next);
                std::scoped_lock lock(g_shotLock);
                if (!g_shot.filming) {
                    return;
                }
                ++g_shot.updates;
                auto* camera = RE::PlayerCamera::GetSingleton();
                auto* root = camera ? camera->cameraRoot.get() : nullptr;
                if (!root) {
                    return;
                }
                if (!g_shot.checked) {
                    CheckConvention(root->world);
                }
                const auto& at = g_shot.follow.shown;
                root->local.translate = { at.x, at.y, at.z };
                root->local.rotate = Rotation(g_shot.aim, g_shot.transposed);
                RE::NiUpdateData data{};
                root->Update(data);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct KnockdownRotationHook
        {
            static void thunk(RE::BleedoutCameraState* state, RE::NiQuaternion& rotation)
            {
                func(state, rotation);
                std::scoped_lock lock(g_shotLock);
                if (g_shot.filming) {
                    ++g_shot.rotations;
                    rotation = RE::NiQuaternion(Rotation(g_shot.aim, g_shot.transposed));
                }
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        struct KnockdownTranslationHook
        {
            static void thunk(RE::BleedoutCameraState* state, RE::NiPoint3& translation)
            {
                func(state, translation);
                std::scoped_lock lock(g_shotLock);
                if (g_shot.filming) {
                    ++g_shot.translations;
                    const auto& at = g_shot.follow.shown;
                    translation = { at.x, at.y, at.z };
                }
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        // Call with g_shotLock held.
        void StopFilming()
        {
            if (!std::exchange(g_shot.filming, false)) {
                return;
            }
            spdlog::info("tumble camera: shot over, looking {:.0f} | knockdown camera calls while filming: {} "
                         "updates, {} rotations, {} positions",
                g_shot.aim.yaw * kDegrees, g_shot.updates, g_shot.rotations, g_shot.translations);
        }
    }

    void Install()
    {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BleedoutCameraState[0] };
        const auto* slots = reinterpret_cast<const std::uintptr_t*>(vtable.address());
        const auto  before = ModuleOf(slots[0x3]);
        KnockdownUpdateHook::func = vtable.write_vfunc(0x3, KnockdownUpdateHook::thunk);
        KnockdownRotationHook::func = vtable.write_vfunc(0x4, KnockdownRotationHook::thunk);
        KnockdownTranslationHook::func = vtable.write_vfunc(0x5, KnockdownTranslationHook::thunk);
        spdlog::info("hooked the knockdown camera (BleedoutCameraState Update, GetRotation, GetTranslation) for the "
                     "tumble shot; its update was {}'s",
            before);
    }

    void StartTumble()
    {
        auto*            camera = RE::PlayerCamera::GetSingleton();
        std::scoped_lock lock(g_shotLock);
        StopFilming();
        g_shot = {};
        if (camera && camera->cameraRoot) {
            g_shot.start = camera->cameraRoot->world.translate;
        }
    }

    void EndTumbleShot()
    {
        std::scoped_lock lock(g_shotLock);
        StopFilming();
    }

    void FilmTumble(RE::PlayerCharacter* player, float delta, bool downed)
    {
        if (!Settings::Camera().enabled) {
            EndTumbleShot();
            SetStatus("off");
            return;
        }
        auto*      camera = RE::PlayerCamera::GetSingleton();
        const bool ragdoll = player->IsInRagdollState();
        std::scoped_lock lock(g_shotLock);
        if (!InKnockdownCamera(camera)) {
            StopFilming();
            // The game switches cameras a frame or two into the ragdoll.
            constexpr int kOtherFrames = 10;
            if (ragdoll && camera && camera->currentState && !g_shot.follow.started &&
                ++g_shot.otherFrames == kOtherFrames) {
                spdlog::info("tumble camera: the ragdoll is in the {} camera, not the game's knockdown camera, so "
                             "there's no tumble shot{}",
                    StateName(camera->currentState->id),
                    g_state.enhancedDeathCamRagdoll ? " (Enhanced Death Cam's ragdoll camera)" : "");
            }
            return;
        }
        if (downed && !ragdoll) {
            StopFilming();  // bleeding out: the game's (or a revive mod's) camera from here
            return;
        }
        if (!g_shot.filming) {
            if (g_shot.follow.started) {
                return;  // already filmed this tumble and handed back
            }
            const auto start = g_shot.start ? *g_shot.start :
                               camera->cameraRoot ? camera->cameraRoot->world.translate : player->GetPosition();
            g_shot.filming = true;
            g_shot.checked = false;
            g_shot.logSeconds = 0.0F;
            Core::StartTumbleCamera(g_shot.follow, ToVec(start), BodyPoint(player));
            spdlog::info("tumble camera: filming from ({:.0f}, {:.0f}, {:.0f}), {}", start.x, start.y, start.z,
                g_shot.start ? "where the camera was at the hit" : "where the knockdown camera is");
        }
        const auto                       settings = Settings::Camera();
        const Core::TumbleCameraSettings shot{ .freezeSeconds = settings.tumbleFreezeSeconds,
            .followSeconds = settings.tumbleFollowSeconds,
            .clearanceUnits = settings.tumbleHeightMetres * kUnitsPerMetre };
        const auto at = g_shot.follow.camera;
        float      groundZ = 0.0F;
        auto*      tes = RE::TES::GetSingleton();
        const bool ground = tes && tes->GetLandHeight({ at.x, at.y, at.z }, groundZ);
        Core::UpdateTumbleCamera(g_shot.follow, shot, delta, BodyPoint(player),
            ground ? std::optional{ groundZ } : std::nullopt);
        const auto clear = ClearUnits(player, g_shot.follow.look, g_shot.follow.camera);
        Core::PlaceTumbleCamera(g_shot.follow, shot, delta, clear);
        g_shot.aim = Core::LookFrom(g_shot.follow.shown, g_shot.follow.look);
        g_shot.aimed = true;
        g_state.follow = {};
        g_state.observed.reset();
        SetStatus("filming the tumble");

        g_shot.logSeconds -= delta;
        if (g_shot.logSeconds <= 0.0F && camera->cameraRoot && Settings::DetailedLog()) {
            g_shot.logSeconds = 0.5F;
            const auto& shown = camera->cameraRoot->world.translate;
            const auto& placed = g_shot.follow.shown;
            const auto& wanted = g_shot.follow.camera;
            spdlog::info("tumble camera: aim yaw {:.0f} pitch {:.0f}, shown yaw {:.0f} | shown {:.0f} from the shot, "
                         "{:.0f} above the ground, {:.0f} from the body, pulled in {:.0f}{} | calls: {} updates, {} "
                         "rotations, {} positions",
                g_shot.aim.yaw * kDegrees, g_shot.aim.pitch * kDegrees,
                FacingYaw(camera->cameraRoot->world.rotate, g_shot.transposed) * kDegrees,
                shown.GetDistance({ placed.x, placed.y, placed.z }), ground ? placed.z - groundZ : -1.0F,
                Core::Length({ placed.x - g_shot.follow.look.x, placed.y - g_shot.follow.look.y,
                    placed.z - g_shot.follow.look.z }),
                Core::Length({ wanted.x - placed.x, wanted.y - placed.y, wanted.z - placed.z }),
                clear ? fmt::format(" (blocked {:.0f} out)", *clear) : "", g_shot.updates, g_shot.rotations,
                g_shot.translations);
        }
    }

    void LeaveKnockdownCamera(RE::PlayerCharacter* player)
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        if (!Settings::Camera().enabled || !InKnockdownCamera(camera)) {
            return;
        }
        std::optional<float> yaw;
        {
            std::scoped_lock lock(g_shotLock);
            StopFilming();
            if (g_shot.aimed) {
                yaw = g_shot.aim.yaw;
            }
        }
        auto& third = camera->GetRuntimeData().cameraStates[RE::CameraState::kThirdPerson];
        if (!third) {
            return;
        }
        camera->SetState(third.get());
        auto* state = static_cast<RE::ThirdPersonState*>(third.get());
        state->freeRotationEnabled = true;
        if (yaw) {
            state->freeRotation.x = WrapSigned(*yaw - player->data.angle.z);
        }
        // The game may ask for its knockdown camera again: logged the first time, then every 30 times.
        static int switches = 0;
        if (switches++ % 30 == 0) {
            spdlog::info("chase camera: back to third person from the knockdown camera{} ({} switches)",
                yaw ? fmt::format(", looking {:.0f}", *yaw * kDegrees) : "", switches);
        }
    }

    void Reset()
    {
        EndTumbleShot();
        g_state.follow = {};
        g_state.observed.reset();
        SetStatus("idle");
    }

    namespace
    {
        std::optional<RE::CameraState> g_watched;
    }

    std::string Describe(RE::PlayerCharacter* player)
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        const auto* state = camera ? camera->currentState.get() : nullptr;
        if (!state) {
            return "camera: no state";
        }
        const auto  character = player->GetPosition();
        std::string text = fmt::format("camera: {} | heading {:.0f}", StateName(state->id), player->data.angle.z * kDegrees);
        if (camera->cameraRoot) {
            const auto& at = camera->cameraRoot->world.translate;
            text += fmt::format(" at ({:.0f}, {:.0f}, {:.0f}), {:.0f} from the character", at.x, at.y, at.z,
                at.GetDistance(character));
        }
        if (state->id == RE::CameraState::kThirdPerson || state->id == RE::CameraState::kBleedout) {
            const auto* third = static_cast<const RE::ThirdPersonState*>(state);
            text += fmt::format(" | orbit {:.0f} pitch {:.0f} zoom {:.2f}{}", third->freeRotation.x * kDegrees,
                third->freeRotation.y * kDegrees, third->currentZoomOffset,
                third->freeRotationEnabled ? "" : ", free rotation off");
        }
        auto* root = player->Get3D(false);
        if (auto* ragdollRoot = root ? root->GetObjectByName("NPC Root [Root]") : nullptr) {
            const auto& body = ragdollRoot->world.translate;
            text += fmt::format(" | body {:.0f} from the character", body.GetDistance(character));
            if (camera->cameraRoot) {
                text += fmt::format(", {:.0f} from the camera", body.GetDistance(camera->cameraRoot->world.translate));
            }
        }
        return text;
    }

    void WatchState(RE::PlayerCharacter* player, const char* label, bool restart)
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        const auto* state = camera ? camera->currentState.get() : nullptr;
        if (!state || (!restart && g_watched == state->id)) {
            return;
        }
        g_watched = state->id;
        spdlog::info("{} | {}", label, Describe(player));
    }

    Info GetInfo() noexcept
    {
        return { g_state.status, g_state.smoothCam, g_state.enhancedDeathCam,
            g_state.enhancedDeathCamRagdoll, g_state.yawDegrees, g_state.lookDegrees };
    }
}
