#include "plugin/Movement.h"

#include "plugin/Skiing.h"

namespace SkiingInSkyrim::Movement
{
    namespace
    {
        constexpr std::size_t kModifyMovementData = 0x11A;

        std::mutex g_sampleLock;
        Sample     g_sample;

        std::mutex g_chainLock;
        ChainInfo  g_chain;

        struct PlayerModifyMovementData
        {
            static void thunk(RE::PlayerCharacter* player, float delta, RE::NiPoint3& movement, RE::NiPoint3& rotation)
            {
                func(player, delta, movement, rotation);
                Skiing::ApplyMovement(player, movement, rotation);
                std::scoped_lock lock(g_sampleLock);
                g_sample = { { movement.x, movement.y, movement.z }, { rotation.x, rotation.y, rotation.z } };
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };

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

        std::uintptr_t* Slot()
        {
            REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PlayerCharacter[0] };
            return reinterpret_cast<std::uintptr_t*>(vtable.address()) + kModifyMovementData;
        }

        void Install()
        {
            // The slot may be called while we patch it, so func must be valid before the thunk is visible.
            PlayerModifyMovementData::func = *Slot();
            REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PlayerCharacter[0] };
            vtable.write_vfunc(kModifyMovementData, PlayerModifyMovementData::thunk);

            const auto below = ModuleOf(PlayerModifyMovementData::func.address());
            spdlog::info("hooked PlayerCharacter::ModifyMovementData (0x11A) on first frame; runs after {}", below);

            std::scoped_lock lock(g_chainLock);
            g_chain.installed = true;
            g_chain.runsBeforeUs = below;
            g_chain.iceIsSlickLoaded = GetModuleHandleA("IceIsSlick.dll") != nullptr;
        }

        void CheckOwner()
        {
            const auto owner = *Slot();
            const bool outermost = owner == reinterpret_cast<std::uintptr_t>(&PlayerModifyMovementData::thunk);

            std::scoped_lock lock(g_chainLock);
            if (outermost == g_chain.outermost && (outermost || !g_chain.runsAfterUs.empty())) {
                return;
            }
            g_chain.outermost = outermost;
            g_chain.runsAfterUs = outermost ? std::string{} : ModuleOf(owner);
            if (outermost) {
                spdlog::info("movement hook is last in the chain");
            } else {
                spdlog::warn("movement hook is wrapped by {}, which runs after us", g_chain.runsAfterUs);
            }
        }
    }

    void Update()
    {
        static bool installed = false;
        if (!installed) {
            installed = true;
            Install();
        }
        CheckOwner();
    }

    Sample Last()
    {
        std::scoped_lock lock(g_sampleLock);
        return g_sample;
    }

    ChainInfo GetChainInfo()
    {
        std::scoped_lock lock(g_chainLock);
        return g_chain;
    }
}
