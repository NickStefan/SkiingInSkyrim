#include "plugin/Sounds.h"

#include "plugin/Settings.h"

namespace SkiingInSkyrim::Sounds
{
    namespace
    {
        // Skyrim.esm sound descriptors: the player's sneaking footsteps (soft, three takes a foot) for strides,
        // and turns and skids when we have none of our own; the ground's jump-landing for landings and crashes.
        struct GroundSounds
        {
            RE::FormID left;
            RE::FormID right;
            RE::FormID landing;
        };
        constexpr GroundSounds kGroundSounds[] = {
            { 0x000B645D, 0x000B645E, 0x0010DA08 },  // snow: FSTPlayerSnowSneakL/RSD, FSTPlayerSnowJumpDownSD
            { 0x000661CD, 0x000661CE, 0x0010DA05 },  // ice: FSTPlayerIceSneakL/R, stone landing
            { 0x000661C9, 0x000661CA, 0x0010DA05 },  // stone: FSTPlayerStoneSolidSneakL/R, ...StoneSolidJumpDownSD
            { 0x000661CB, 0x000661CC, 0x0010DA04 },  // wood
            { 0x000661C7, 0x000661C8, 0x0010DA06 },  // gravel
            { 0x000661C5, 0x000661C6, 0x0010DA07 },  // grass
            { 0x000661C3, 0x000661C4, 0x0010DA03 },  // dirt
        };
        // Our own, in SkiingInSkyrim.esp (tools/make_plugin.py), when the build had recordings for them.
        constexpr auto       kPlugin = "SkiingInSkyrim.esp";
        constexpr RE::FormID kPackedTurn = 0x804;
        constexpr RE::FormID kPackedSkid = 0x805;
        constexpr RE::FormID kPowderTurn = 0x806;
        constexpr RE::FormID kPowderSkid = 0x807;

        // Our sounds for one kind (turns or skids); either may be missing.
        struct SnowSounds
        {
            RE::BGSSoundDescriptorForm* powder{ nullptr };
            RE::BGSSoundDescriptorForm* packed{ nullptr };
        };

        std::atomic<bool> g_hooked{ false };
        std::atomic<bool> g_silencing{ false };

        std::mutex          g_infoLock;
        Info                g_info;
        Core::SkiSoundState g_state;
        bool                g_looked{ false };
        bool                g_found{ false };
        SnowSounds          g_turns;
        SnowSounds          g_skids;

        struct FootstepHook
        {
            static RE::BSEventNotifyControl thunk(RE::BSTEventSink<RE::BGSFootstepEvent>* sink,
                const RE::BGSFootstepEvent* event, RE::BSTEventSource<RE::BGSFootstepEvent>* source)
            {
                if (event && g_silencing.load(std::memory_order_relaxed)) {
                    const auto actor = event->actor.get();
                    if (actor && actor.get() == RE::PlayerCharacter::GetSingleton()) {
                        return RE::BSEventNotifyControl::kContinue;
                    }
                }
                return func(sink, event, source);
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };

        RE::BGSSoundDescriptorForm* Descriptor(RE::FormID id)
        {
            return RE::TESForm::LookupByID<RE::BGSSoundDescriptorForm>(id);
        }

        RE::BGSSoundDescriptorForm* Ours(RE::FormID id)
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            return data ? data->LookupForm<RE::BGSSoundDescriptorForm>(id, kPlugin) : nullptr;
        }

        void LookUp()
        {
            if (std::exchange(g_looked, true)) {
                return;
            }
            g_found = true;
            for (const auto& sounds : kGroundSounds) {
                for (const auto id : { sounds.left, sounds.right, sounds.landing }) {
                    if (!Descriptor(id)) {
                        spdlog::warn("sound: Skyrim.esm sound descriptor {:08X} not found", id);
                        g_found = false;
                    }
                }
            }
            g_turns = { Ours(kPowderTurn), Ours(kPackedTurn) };
            g_skids = { Ours(kPowderSkid), Ours(kPackedSkid) };
            const auto have = [](const SnowSounds& sounds) {
                return sounds.powder && sounds.packed ? "powder and packed" :
                       sounds.powder                  ? "powder only" :
                       sounds.packed                  ? "packed only" :
                                                        "none (the game's)";
            };
            spdlog::info("sound: our turns: {}; skids: {}{}", have(g_turns), have(g_skids),
                g_found ? "" : "; some of the game's sounds missing");
        }

        void Play(RE::PlayerCharacter* player, RE::BGSSoundDescriptorForm* descriptor, float volume, float pitch)
        {
            auto* audio = RE::BSAudioManager::GetSingleton();
            if (!descriptor || !audio || volume <= 0.001F) {
                return;
            }
            RE::BSSoundHandle sound;
            if (!audio->GetSoundHandle(sound, descriptor) || !sound.IsValid()) {
                return;
            }
            if (auto* node = player->Get3D()) {
                sound.SetObjectToFollow(node);
            }
            sound.SetVolume(volume);
            sound.SetFrequency(pitch);
            sound.Play();
        }

        // On any ground: powder and packed sounds by the powder's share, one standing in for the other if missing,
        // the game's footstep if we have neither.
        void PlaySnow(RE::PlayerCharacter* player, const SnowSounds& sounds, RE::BGSSoundDescriptorForm* footstep,
            const Core::SoundGrain& grain, float footstepPitch)
        {
            if (!sounds.powder || !sounds.packed) {
                auto* only = sounds.powder ? sounds.powder : sounds.packed;
                Play(player, only ? only : footstep, grain.volume, only ? grain.pitch : footstepPitch);
                return;
            }
            const auto shares = Core::ShareByPowder(grain.volume, grain.powder);
            Play(player, sounds.powder, shares.powder, grain.pitch);
            Play(player, sounds.packed, shares.packed, grain.pitch);
        }
    }

    void Install()
    {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BGSImpactManager[0] };
        FootstepHook::func = vtable.write_vfunc(0x1, FootstepHook::thunk);
        g_hooked = true;
        spdlog::info("hooked footstep sounds (BGSImpactManager), to keep them quiet while skiing");
    }

    Ground GroundOf(RE::MATERIAL_ID underfoot, RE::MATERIAL_ID land)
    {
        using enum RE::MATERIAL_ID;
        switch (underfoot != kNone ? underfoot : land) {
        case kIce:
        case kIceForm:
            return Ground::Ice;
        case kStone:
        case kStoneBroken:
        case kStoneHeavy:
        case kStoneStairs:
        case kBoulderSmall:
        case kBoulderMedium:
        case kBoulderLarge:
            return Ground::Stone;
        case kWood:
        case kWoodStairs:
        case kCarpet:
            return Ground::Wood;
        case kGravel:
            return Ground::Gravel;
        case kGrass:
            return Ground::Grass;
        case kDirt:
        case kMud:
        case kSand:
            return Ground::Dirt;
        default:
            return Ground::Snow;
        }
    }

    const char* GroundName(Ground ground)
    {
        switch (ground) {
        case Ground::Snow:   return "snow";
        case Ground::Ice:    return "ice";
        case Ground::Stone:  return "stone";
        case Ground::Wood:   return "wood";
        case Ground::Gravel: return "gravel";
        case Ground::Grass:  return "grass";
        case Ground::Dirt:   return "dirt";
        }
        return "snow";
    }

    void Update(RE::PlayerCharacter* player, const Core::SkiSoundInput& input, Ground ground, Ground crashed)
    {
        const auto settings = Settings::Sound();
        g_silencing = settings.silenceFootsteps;
        LookUp();
        const auto  mix = Core::MixSkiSound(g_state, input, settings);
        const auto& sounds = kGroundSounds[static_cast<std::size_t>(ground)];
        for (std::size_t i = 0; i < mix.grainCount; ++i) {
            const auto& grain = mix.grains[i];
            auto*       footstep = Descriptor(grain.right ? sounds.right : sounds.left);
            switch (grain.kind) {
            case Core::SoundKind::Turn:
                PlaySnow(player, g_turns, footstep, grain, grain.pitch);
                break;
            case Core::SoundKind::Skid:
                PlaySnow(player, g_skids, footstep, grain, grain.pitch * 0.8F);
                break;
            case Core::SoundKind::Stride:
                Play(player, footstep, grain.volume, grain.pitch);
                break;
            case Core::SoundKind::Landing:
                Play(player, Descriptor(sounds.landing), grain.volume, grain.pitch);
                break;
            case Core::SoundKind::Crash:
                Play(player, Descriptor(kGroundSounds[static_cast<std::size_t>(crashed)].landing), grain.volume, grain.pitch);
                break;
            }
        }
        std::scoped_lock lock(g_infoLock);
        g_info.ground = GroundName(ground);
    }

    void Stop()
    {
        g_silencing = false;
        g_state.skidding = false;
        g_state.turnSide = 0.0F;
        std::scoped_lock lock(g_infoLock);
        g_info.ground = "";
    }

    void PlayFlow(RE::PlayerCharacter* player)
    {
        constexpr RE::FormID kFlowSound = 0x000C8C77;  // UIEnchantingLearnEffect, filling and emptying alike
        const auto settings = Settings::Sound();
        if (settings.enabled) {
            Play(player, Descriptor(kFlowSound), settings.volume, 1.0F);
        }
    }

    void MuteRagdollLoop(bool muted)
    {
        // VOCShoutFXSlowTimeActiveLPSD (mag_cloak_active_lp.wav): the loop Immersive Sounds Compendium's
        // "Silent Player Ragdoll" option silences everywhere.
        constexpr RE::FormID    kRagdollLoop = 0x0003F593;
        constexpr std::uint16_t kSilent = 10000;  // static attenuation, 1/100 dB
        static std::optional<std::uint16_t> s_attenuation;
        auto* form = Descriptor(kRagdollLoop);
        auto* sound = form ? skyrim_cast<RE::BGSStandardSoundDef*>(form->soundDescriptor) : nullptr;
        if (!sound || muted == s_attenuation.has_value()) {
            return;
        }
        auto& attenuation = sound->soundCharacteristics.staticAttenuation;
        if (muted) {
            s_attenuation = attenuation;
            attenuation = kSilent;
        } else {
            attenuation = *s_attenuation;
            s_attenuation.reset();
        }
    }

    Info GetInfo()
    {
        std::scoped_lock lock(g_infoLock);
        auto info = g_info;
        info.hooked = g_hooked;
        info.silencing = g_silencing;
        return info;
    }
}
