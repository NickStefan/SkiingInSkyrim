#include "plugin/Skis.h"

#include "core/SkiPose.h"
#include "plugin/SkiingState.h"

namespace SkiingInSkyrim::Skis
{
    namespace
    {
        constexpr const char* kPartNames[2] = { "SkiLeft", "SkiRight" };
        constexpr const char* kFootBones[2] = { "NPC L Foot [Lft ]", "NPC R Foot [Rft ]" };

        // Game units. A model without SkiLeft and SkiRight nodes is taken to be one ski and scaled to this
        // length (1.8 m); one copy goes under each foot.
        constexpr float kOneSkiLength = 1.8F * 70.0F;
        // Half a ski's width: how far an edged ski is lifted onto its edge.
        constexpr float kHalfWidth = 5.0F;
        // How far right of the character each ski's centre sits (left is negative): where the foot is, kept
        // within these, smoothed over kStanceSeconds so the animation's footwork doesn't shuffle them.
        constexpr float kStanceDefault = 8.0F;
        constexpr float kStanceMin = 6.0F;
        constexpr float kStanceMax = 20.0F;
        constexpr float kStanceSeconds = 0.15F;

        // The equip event can come before the item is marked worn, so look again for a few frames after.
        constexpr int kRescanFrames = 10;

        std::atomic<int>   g_rescan{ kRescanFrames };
        RE::TESObjectARMO* g_worn = nullptr;

        struct Visual
        {
            RE::TESObjectARMO*                           armor{ nullptr };
            bool                                         failed{ false };
            std::vector<RE::NiPointer<RE::NiNode>>       roots;  // attached to the character: the model, or two copies
            std::array<RE::NiPointer<RE::NiAVObject>, 2> parts;  // left, right
            float                                        scale{ 1.0F };
            RE::NiNode*                                  attachedTo{ nullptr };
            std::array<RE::NiPointer<RE::NiAVObject>, 2> feet;
            std::array<float, 2>                         stance{ -kStanceDefault, kStanceDefault };
            bool                                         stanceKnown{ false };
        };
        Visual g_visual;

        RE::TESObjectARMO* FindWorn(RE::PlayerCharacter* player)
        {
            const auto inventory = player->GetInventory([](RE::TESBoundObject& object) { return object.IsArmor(); });
            for (const auto& [object, entry] : inventory) {
                auto* armor = object->As<RE::TESObjectARMO>();
                if (armor && entry.second && entry.second->IsWorn() && armor->HasKeywordString(kKeyword)) {
                    return armor;
                }
            }
            return nullptr;
        }

        RE::NiPointer<RE::NiNode> Load(const std::string& path)
        {
            const RE::BSModelDB::DBTraits::ArgsType args;
            for (const auto& candidate : { path, "Meshes\\" + path }) {
                RE::NiPointer<RE::NiNode> model;
                if (RE::BSModelDB::Demand(candidate.c_str(), model, args) == RE::BSResource::ErrorCode::kNone && model) {
                    return model;
                }
            }
            return nullptr;
        }

        void StripCollision(RE::NiAVObject* object)
        {
            if (!object) {
                return;
            }
            object->collisionObject.reset();
            if (auto* node = object->AsNode()) {
                for (auto& child : node->GetChildren()) {
                    StripCollision(child.get());
                }
            }
        }

        // The model database shares what it loads, and each copy can only hang in one place.
        RE::NiPointer<RE::NiNode> Copy(RE::NiNode* model)
        {
            auto* copy = model->Clone();
            RE::NiPointer<RE::NiNode> node{ copy ? copy->AsNode() : nullptr };
            StripCollision(node.get());
            return node;
        }

        void Detach()
        {
            for (auto& root : g_visual.roots) {
                if (g_visual.attachedTo && root->parent == g_visual.attachedTo) {
                    g_visual.attachedTo->DetachChild2(root.get());
                }
            }
            g_visual.attachedTo = nullptr;
            g_visual.feet = {};
            g_visual.stanceKnown = false;
        }

        void Build(RE::TESObjectARMO* armor)
        {
            Detach();
            g_visual = {};
            g_visual.armor = armor;
            const std::string path = armor->worldModels[RE::TESBipedModelForm::Sexes::kMale].GetModel();
            const auto        model = path.empty() ? nullptr : Load(path);
            if (!model) {
                spdlog::warn("skis {:08X}: could not load world model '{}'", armor->GetFormID(), path);
                g_visual.failed = true;
                return;
            }
            auto first = Copy(model.get());
            if (!first) {
                g_visual.failed = true;
                return;
            }
            auto* left = first->GetObjectByName(kPartNames[0]);
            auto* right = first->GetObjectByName(kPartNames[1]);
            if (left && right) {
                first->local = {};
                g_visual.parts = { RE::NiPointer<RE::NiAVObject>(left), RE::NiPointer<RE::NiAVObject>(right) };
                g_visual.roots = { first };
                spdlog::info("skis {:08X}: '{}' has {} and {}", armor->GetFormID(), path, kPartNames[0], kPartNames[1]);
                return;
            }
            auto second = Copy(model.get());
            if (!second) {
                g_visual.failed = true;
                return;
            }
            RE::NiUpdateData update{};
            first->Update(update);
            const float radius = first->worldBound.radius;
            g_visual.scale = radius > 1.0F ? kOneSkiLength * 0.5F / radius : 1.0F;
            g_visual.parts = { RE::NiPointer<RE::NiAVObject>(first.get()), RE::NiPointer<RE::NiAVObject>(second.get()) };
            g_visual.roots = { first, second };
            spdlog::info("skis {:08X}: '{}' has no {}/{} nodes; one copy per foot, scaled {:.2f}", armor->GetFormID(),
                path, kPartNames[0], kPartNames[1], g_visual.scale);
        }

        void Attach(RE::NiNode* character)
        {
            for (auto& root : g_visual.roots) {
                character->AttachChild(root.get(), true);
            }
            g_visual.attachedTo = character;
            for (std::size_t i = 0; i < 2; ++i) {
                g_visual.feet[i].reset(character->GetObjectByName(kFootBones[i]));
            }
            if (!g_visual.feet[0] || !g_visual.feet[1]) {
                spdlog::info("skis: no foot bones on the character; using a fixed stance");
            }
        }

        // How far right of the character's origin a foot is, from where the skeleton last put it.
        float FootLateral(const RE::NiAVObject* foot, const RE::NiAVObject& character, float fallback)
        {
            if (!foot || character.world.scale <= 0.0F) {
                return fallback;
            }
            const auto  offset = foot->world.translate - character.world.translate;
            const auto& rotate = character.world.rotate.entry;
            return (rotate[0][0] * offset.x + rotate[1][0] * offset.y + rotate[2][0] * offset.z) / character.world.scale;
        }

        void SetLocal(RE::NiAVObject& part, const Core::SkiPose& pose, float scale)
        {
            const Core::Vec3 axes[3] = { pose.right, pose.forward, pose.up };
            for (int column = 0; column < 3; ++column) {
                part.local.rotate.entry[0][column] = axes[column].x;
                part.local.rotate.entry[1][column] = axes[column].y;
                part.local.rotate.entry[2][column] = axes[column].z;
            }
            part.local.translate = { pose.position.x, pose.position.y, pose.position.z };
            part.local.scale = scale;
        }

        class EquipSink final : public RE::BSTEventSink<RE::TESEquipEvent>
        {
        public:
            RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* event, RE::BSTEventSource<RE::TESEquipEvent>*) override
            {
                if (event && event->actor && event->actor->IsPlayerRef()) {
                    g_rescan = kRescanFrames;
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
        EquipSink g_equipSink;
    }

    void Register()
    {
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESEquipEvent>(&g_equipSink);
            spdlog::info("watching equipment for skis ({})", kKeyword);
        } else {
            spdlog::error("no script event source; worn skis won't be noticed");
        }
    }

    void Reset()
    {
        Detach();
        g_visual = {};
        g_worn = nullptr;
        SetSkisWorn(false);
        g_rescan = kRescanFrames;
    }

    void Refresh() { g_rescan = kRescanFrames; }

    RE::TESObjectARMO* Worn(RE::PlayerCharacter* player)
    {
        if (g_rescan.load() > 0) {
            --g_rescan;
            auto* worn = FindWorn(player);
            if (worn != g_worn) {
                if (worn) {
                    spdlog::info("skis on: {} ({:08X})", worn->GetName(), worn->GetFormID());
                } else {
                    spdlog::info("skis off");
                }
                g_worn = worn;
            }
            SetSkisWorn(worn != nullptr);
        }
        return g_worn;
    }

    void Show(RE::PlayerCharacter* player, RE::TESObjectARMO* skis, const Pose& pose, float delta)
    {
        if (!skis) {
            Hide();
            return;
        }
        if (skis != g_visual.armor) {
            Build(skis);
        }
        auto* character3D = player->Get3D(false);
        auto* character = character3D ? character3D->AsNode() : nullptr;
        if (g_visual.failed || g_visual.roots.empty() || !character) {
            Detach();
            return;
        }
        if (g_visual.attachedTo != character || g_visual.roots.front()->parent != character) {
            Detach();
            Attach(character);
        }

        const float blend = g_visual.stanceKnown ? 1.0F - std::exp(-std::max(0.0F, delta) / kStanceSeconds) : 1.0F;
        for (std::size_t i = 0; i < 2; ++i) {
            const float side = i == 0 ? -1.0F : 1.0F;
            const float lateral = FootLateral(g_visual.feet[i].get(), *character, side * kStanceDefault) * side;
            const float target = side * std::clamp(lateral, kStanceMin, kStanceMax);
            g_visual.stance[i] += (target - g_visual.stance[i]) * blend;
        }
        g_visual.stanceKnown = true;

        // Into the character's frame (x right, y forward), which is how the character's 3D will be turned.
        const float heading = player->data.angle.z;
        const Core::Vec3 normal{
            pose.groundNormal.x * std::cos(heading) - pose.groundNormal.y * std::sin(heading),
            pose.groundNormal.x * std::sin(heading) + pose.groundNormal.y * std::cos(heading),
            pose.groundNormal.z,
        };
        const float yaw = std::remainder(pose.yaw - heading, 2.0F * std::numbers::pi_v<float>);
        for (std::size_t i = 0; i < 2; ++i) {
            SetLocal(*g_visual.parts[i], Core::PlaceSki(normal, yaw, pose.edgeRadians, g_visual.stance[i], kHalfWidth),
                g_visual.scale);
        }
        RE::NiUpdateData update{};
        for (auto& root : g_visual.roots) {
            root->Update(update);
        }
    }

    void Hide() { Detach(); }
}
