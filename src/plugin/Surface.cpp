#include "plugin/Surface.h"

#include "core/Snow.h"

#include <array>

namespace SkiingInSkyrim::Surface
{
    namespace
    {
        // The cover is read on a square of points this far apart (game units) around the feet.
        constexpr float kSampleSpacing = 48.0F;
        constexpr float kDownFrom = 40.0F;
        constexpr float kDownTo = -80.0F;
        // The feet are on the landscape within this many units of its height; objects stand higher.
        constexpr float kOnLandUnits = 24.0F;

        std::string Lower(std::string_view text)
        {
            std::string lower(text);
            std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return lower;
        }

        std::string TextureOf(const RE::TESLandTexture* land)
        {
            const auto* set = land ? land->textureSet : nullptr;
            const char* name = set ? set->textures[0].textureName.c_str() : nullptr;
            return name ? Lower(name) : std::string();
        }

        bool Contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

        bool IsSnow(RE::MATERIAL_ID material)
        {
            return material == RE::MATERIAL_ID::kSnow || material == RE::MATERIAL_ID::kSnowStairs;
        }

        // The first thing between two points (game units) that the player's character controller collides with,
        // not counting the player.
        std::optional<RE::COL_LAYER> Ray(RE::bhkWorld* world, RE::bhkCharacterController* controller,
            const RE::NiPoint3& from, const RE::NiPoint3& to)
        {
            RE::CFilter own;
            controller->GetCollisionFilterInfo(own);
            const float     scale = RE::bhkWorld::GetWorldScale();
            RE::bhkPickData pick;
            pick.rayInput.from = RE::hkVector4(from.x * scale, from.y * scale, from.z * scale, 0.0F);
            pick.rayInput.to = RE::hkVector4(to.x * scale, to.y * scale, to.z * scale, 0.0F);
            pick.rayInput.filterInfo.filter =
                (own.GetSystemGroup() << 16) | static_cast<std::uint32_t>(RE::COL_LAYER::kCharController);
            world->PickObject(pick);
            const auto* collidable = pick.rayOutput.rootCollidable;
            if (!pick.rayOutput.HasHit() || !collidable) {
                return std::nullopt;
            }
            return collidable->GetCollisionLayer();
        }
    }

    Reading Read(RE::PlayerCharacter* player, RE::bhkCharacterController* controller)
    {
        Reading reading;
        reading.underfoot = controller->surfaceMaterial;
        auto* tes = RE::TES::GetSingleton();
        auto* cell = player->GetParentCell();
        auto* world = cell ? cell->GetbhkWorld() : nullptr;
        if (!tes || !world || cell->IsInteriorCell()) {
            return reading;
        }
        const auto p = player->GetPosition();
        reading.land = tes->GetLandMaterialType(p);
        reading.texture = TextureOf(tes->GetLandTexture(p));

        std::array<Core::SnowSample, 9> samples;
        for (int i = 0; i < 9; ++i) {
            const RE::NiPoint3 at{ p.x + static_cast<float>(i % 3 - 1) * kSampleSpacing,
                p.y + static_cast<float>(i / 3 - 1) * kSampleSpacing, p.z };
            const auto texture = i == 4 ? reading.texture : TextureOf(tes->GetLandTexture(at));
            samples[i] = { .snow = IsSnow(i == 4 ? reading.land : tes->GetLandMaterialType(at)),
                .packed = Contains(texture, "road") || Contains(texture, "path") || Contains(texture, "trail") };
        }
        reading.powderCover = Core::PowderCover(samples);

        // The landscape's collision is mostly on the ground layer, now and then terrain.
        const auto down = Ray(world, controller, { p.x, p.y, p.z + kDownFrom }, { p.x, p.y, p.z + kDownTo });
        reading.below = down.value_or(RE::COL_LAYER::kUnidentified);
        float landZ = 0.0F;
        const bool landKnown = tes->GetLandHeight(p, landZ);
        reading.aboveLandUnits = landKnown ? p.z - landZ : 0.0F;
        reading.onLand = (down == RE::COL_LAYER::kGround || down == RE::COL_LAYER::kTerrain) &&
                         (!landKnown || reading.aboveLandUnits < kOnLandUnits);
        return reading;
    }

    const char* MaterialName(RE::MATERIAL_ID material)
    {
        using enum RE::MATERIAL_ID;
        switch (material) {
        case kNone:        return "none";
        case kSnow:        return "snow";
        case kSnowStairs:  return "snow stairs";
        case kIce:         return "ice";
        case kIceForm:     return "ice form";
        case kStone:       return "stone";
        case kStoneBroken: return "broken stone";
        case kStoneHeavy:  return "heavy stone";
        case kStoneStairs: return "stone stairs";
        case kGravel:      return "gravel";
        case kDirt:        return "dirt";
        case kGrass:       return "grass";
        case kMud:         return "mud";
        case kSand:        return "sand";
        case kWood:        return "wood";
        case kWoodStairs:  return "wood stairs";
        case kWater:       return "water";
        case kWaterPuddle: return "puddle";
        case kCarpet:      return "carpet";
        case kBoulderSmall:
        case kBoulderMedium:
        case kBoulderLarge: return "boulder";
        default:           return "other";
        }
    }
}
