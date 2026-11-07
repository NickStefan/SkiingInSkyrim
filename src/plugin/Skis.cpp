#include "plugin/Skis.h"

#include "core/SkiPose.h"
#include "plugin/Skeleton.h"
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
        // How far right of the character each ski's centre sits (left is negative): where the foot is, kept
        // within these, smoothed over kStanceSeconds so the footwork doesn't shuffle them.
        constexpr float kStanceDefault = 8.0F;
        constexpr float kStanceMin = 6.0F;
        constexpr float kStanceMax = 20.0F;
        constexpr float kStanceSeconds = 0.15F;

        // The equip event can come before the item is marked worn, so look again for a few frames after.
        constexpr int kRescanFrames = 10;

        std::atomic<int>   g_rescan{ kRescanFrames };
        RE::TESObjectARMO* g_worn = nullptr;  // main thread

        // Copies of the skis' model, not yet attached to anything.
        struct Model
        {
            RE::TESObjectARMO*                           armor{ nullptr };
            bool                                         failed{ false };
            std::vector<RE::NiPointer<RE::NiNode>>       roots;  // attached to the character: the model, or two copies
            std::array<RE::NiPointer<RE::NiAVObject>, 2> parts;  // left, right
            float                                        scale{ 1.0F };
        };

        struct Placed
        {
            Model                                        model;
            RE::NiNode*                                  attachedTo{ nullptr };
            std::array<RE::NiPointer<RE::NiAVObject>, 2> feet;
            std::array<float, 2>                         stance{ -kStanceDefault, kStanceDefault };
            bool                                         stanceKnown{ false };
        };

        std::mutex g_lock;
        Model      g_prepared;  // built on the main thread
        Placed     g_placed;    // in use on the animation thread

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

        Model Build(RE::TESObjectARMO* armor)
        {
            Model result{ .armor = armor };
            const std::string path = armor->worldModels[RE::TESBipedModelForm::Sexes::kMale].GetModel();
            const auto        model = path.empty() ? nullptr : Skeleton::LoadModel(path);
            auto              first = model ? Copy(model.get()) : nullptr;
            if (!first) {
                spdlog::warn("skis {:08X}: could not load world model '{}'", armor->GetFormID(), path);
                result.failed = true;
                return result;
            }
            auto* left = first->GetObjectByName(kPartNames[0]);
            auto* right = first->GetObjectByName(kPartNames[1]);
            if (left && right) {
                first->local = {};
                result.parts = { RE::NiPointer<RE::NiAVObject>(left), RE::NiPointer<RE::NiAVObject>(right) };
                result.roots = { first };
                spdlog::info("skis {:08X}: '{}' has {} and {}", armor->GetFormID(), path, kPartNames[0], kPartNames[1]);
                return result;
            }
            auto second = Copy(model.get());
            if (!second) {
                result.failed = true;
                return result;
            }
            RE::NiUpdateData update{};
            first->Update(update);
            const float radius = first->worldBound.radius;
            result.scale = radius > 1.0F ? kOneSkiLength * 0.5F / radius : 1.0F;
            result.parts = { RE::NiPointer<RE::NiAVObject>(first.get()), RE::NiPointer<RE::NiAVObject>(second.get()) };
            result.roots = { first, second };
            spdlog::info("skis {:08X}: '{}' has no {}/{} nodes; one copy per foot, scaled {:.2f}", armor->GetFormID(),
                path, kPartNames[0], kPartNames[1], result.scale);
            return result;
        }

        void Detach(Placed& placed)
        {
            for (auto& root : placed.model.roots) {
                if (placed.attachedTo && root->parent == placed.attachedTo) {
                    placed.attachedTo->DetachChild2(root.get());
                }
            }
            placed.attachedTo = nullptr;
            placed.feet = {};
            placed.stanceKnown = false;
        }

        void Attach(Placed& placed, RE::NiNode* character)
        {
            for (auto& root : placed.model.roots) {
                character->AttachChild(root.get(), true);
            }
            placed.attachedTo = character;
            for (std::size_t i = 0; i < 2; ++i) {
                placed.feet[i].reset(character->GetObjectByName(kFootBones[i]));
            }
            if (!placed.feet[0] || !placed.feet[1]) {
                spdlog::info("skis: no foot bones on the character; using a fixed stance");
            }
        }

        void SetLocal(RE::NiAVObject& part, const Core::SkiPose& pose, float scale)
        {
            part.local.rotate = Skeleton::FromAxes(pose.right, pose.forward, pose.up);
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
        {
            std::scoped_lock lock(g_lock);
            Detach(g_placed);
            g_placed = {};
            g_prepared = {};
        }
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

    void Prepare(RE::TESObjectARMO* skis)
    {
        if (!skis) {
            return;
        }
        {
            std::scoped_lock lock(g_lock);
            if (g_prepared.armor == skis) {
                return;
            }
        }
        auto model = Build(skis);
        std::scoped_lock lock(g_lock);
        g_prepared = std::move(model);
    }

    void Place(RE::PlayerCharacter* player, const Skiing::Body& body, float delta, const std::array<float, 2>* stance)
    {
        std::scoped_lock lock(g_lock);
        if (!body.active || !body.skis) {
            Detach(g_placed);
            return;
        }
        if (g_placed.model.armor != g_prepared.armor) {
            Detach(g_placed);
            g_placed = { .model = g_prepared };
        }
        auto* character3D = player->Get3D(false);
        auto* character = character3D ? character3D->AsNode() : nullptr;
        if (g_placed.model.armor != body.skis || g_placed.model.failed || g_placed.model.roots.empty() || !character) {
            Detach(g_placed);
            return;
        }
        if (g_placed.attachedTo != character || g_placed.model.roots.front()->parent != character) {
            Detach(g_placed);
            Attach(g_placed, character);
        }

        const float blend = g_placed.stanceKnown ? 1.0F - std::exp(-std::max(0.0F, delta) / kStanceSeconds) : 1.0F;
        if (stance) {
            g_placed.stance = *stance;
        } else {
            for (std::size_t i = 0; i < 2; ++i) {
                const float side = i == 0 ? -1.0F : 1.0F;
                const auto* foot = g_placed.feet[i].get();
                const float lateral = foot ? Skeleton::Relative(foot, character).translate.x * side : kStanceDefault;
                const float target = side * std::clamp(lateral, kStanceMin, kStanceMax);
                g_placed.stance[i] += (target - g_placed.stance[i]) * blend;
            }
        }
        g_placed.stanceKnown = true;

        const float heading = player->data.angle.z;
        const auto  normal = Skeleton::ToCharacter(body.groundNormal, heading);
        const float yaw = std::remainder(body.yaw - heading, 2.0F * std::numbers::pi_v<float>);
        for (std::size_t i = 0; i < 2; ++i) {
            SetLocal(*g_placed.model.parts[i], Core::PlaceSki(normal, yaw, body.edgeRadians, g_placed.stance[i], kHalfWidth),
                g_placed.model.scale);
        }
        RE::NiUpdateData update{};
        for (auto& root : g_placed.model.roots) {
            root->Update(update);
        }
    }
}
