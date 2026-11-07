#include "plugin/Pose.h"

#include "core/SkiPose.h"
#include "core/Stance.h"
#include "plugin/Settings.h"
#include "plugin/Skeleton.h"
#include "plugin/Skiing.h"
#include "plugin/Skis.h"

namespace SkiingInSkyrim::Pose
{
    namespace
    {
        // Parents before children.
        enum Bone : std::size_t
        {
            kCom,
            kPelvis,
            kLeftThigh,
            kLeftCalf,
            kLeftFoot,
            kRightThigh,
            kRightCalf,
            kRightFoot,
            kSpine,
            kSpine1,
            kSpine2,
            kNeck,
            kLeftClavicle,
            kLeftUpperArm,
            kLeftForearm,
            kLeftHand,
            kRightClavicle,
            kRightUpperArm,
            kRightForearm,
            kRightHand,
            kBoneCount
        };

        constexpr const char* kBoneNames[kBoneCount] = {
            "NPC COM [COM ]",
            "NPC Pelvis [Pelv]",
            "NPC L Thigh [LThg]",
            "NPC L Calf [LClf]",
            "NPC L Foot [Lft ]",
            "NPC R Thigh [RThg]",
            "NPC R Calf [RClf]",
            "NPC R Foot [Rft ]",
            "NPC Spine [Spn0]",
            "NPC Spine1 [Spn1]",
            "NPC Spine2 [Spn2]",
            "NPC Neck [Neck]",
            "NPC L Clavicle [LClv]",
            "NPC L UpperArm [LUar]",
            "NPC L Forearm [LLar]",
            "NPC L Hand [LHnd]",
            "NPC R Clavicle [RClv]",
            "NPC R UpperArm [RUar]",
            "NPC R Forearm [RLar]",
            "NPC R Hand [RHnd]",
        };

        constexpr Bone kThigh[2] = { kLeftThigh, kRightThigh };
        constexpr Bone kCalf[2] = { kLeftCalf, kRightCalf };
        constexpr Bone kFoot[2] = { kLeftFoot, kRightFoot };
        constexpr Bone kClavicle[2] = { kLeftClavicle, kRightClavicle };
        constexpr Bone kUpperArm[2] = { kLeftUpperArm, kRightUpperArm };
        constexpr Bone kForearm[2] = { kLeftForearm, kRightForearm };
        constexpr Bone kHand[2] = { kLeftHand, kRightHand };

        // How much of the torso's lean each bone above the hips takes.
        constexpr float kPelvisLean = 0.25F;
        constexpr float kSpineLean[3] = { 0.5F, 0.75F, 1.0F };
        constexpr float kNeckLean = 0.3F;
        // Leaning into a turn: how far up the spine each bone has gone from the hips' roll to the upper body's,
        // and how much of the upper body's the neck keeps, holding the head nearer level.
        constexpr float kSpineRoll[3] = { 0.5F, 0.8F, 1.0F };
        constexpr float kNeckRoll = 0.5F;

        // Seconds to blend the stance in as skiing starts and out as it stops.
        constexpr float kBlendSeconds = 0.3F;

        // The skeleton's standing pose, relative to its root: the stance is built from it, so whatever the
        // animation is doing underneath doesn't show through.
        struct Standing
        {
            std::string                             skeleton;
            bool                                    valid{ false };
            std::array<RE::NiTransform, kBoneCount> pose;
            float                                   thigh{ 0.0F };  // lengths, game units
            float                                   shin{ 0.0F };
        };

        std::mutex g_standingLock;
        Standing   g_standing;

        // Animation thread only.
        struct Live
        {
            const RE::NiAVObject*                                root{ nullptr };
            std::array<RE::NiPointer<RE::NiAVObject>, kBoneCount> bones;
            bool                                                 complete{ false };
        };
        Live  g_live;
        float g_weight = 0.0F;

        const RE::NiPoint3 kRight{ 1.0F, 0.0F, 0.0F };
        const RE::NiPoint3 kForward{ 0.0F, 1.0F, 0.0F };

        // Positive swings a bone pointing down forward, and tips one pointing up back.
        RE::NiMatrix3 Pitch(float radians) { return Skeleton::AxisAngle(kRight, radians); }
        // Positive leans the top to the right.
        RE::NiMatrix3 Roll(float radians) { return Skeleton::AxisAngle(kForward, radians); }

        RE::NiPoint3 ToPoint(const Core::Vec3& v) { return { v.x, v.y, v.z }; }
        Core::Vec3   ToVec(const RE::NiPoint3& p) { return { p.x, p.y, p.z }; }

        // A rotation whose first column is along direction and second along hinge (made square to it).
        RE::NiMatrix3 Frame(const RE::NiPoint3& direction, const RE::NiPoint3& hinge)
        {
            auto along = direction;
            along.Unitize();
            auto across = hinge - along * along.Dot(hinge);
            across.Unitize();
            return Skeleton::FromAxes(ToVec(along), ToVec(across), ToVec(along.Cross(across)));
        }

        // Turns a bone that ran along from (knee hinge fromHinge) to run along to (hinge toHinge).
        RE::NiMatrix3 Turn(const RE::NiPoint3& from, const RE::NiPoint3& fromHinge, const RE::NiPoint3& to,
            const RE::NiPoint3& toHinge)
        {
            return Frame(to, toHinge) * Frame(from, fromHinge).Transpose();
        }

        void Find(RE::NiAVObject* character)
        {
            g_live = { .root = character };
            g_live.complete = true;
            for (std::size_t i = 0; i < kBoneCount; ++i) {
                auto* bone = character->GetObjectByName(kBoneNames[i]);
                g_live.bones[i].reset(bone);
                g_live.complete &= bone && bone->parent;
            }
        }

        // Sets bone so that, relative to the character, it has the rotation (and, if given, the position)
        // target; weight blends from what the animation gave it.
        void Set(RE::NiAVObject& bone, const RE::NiAVObject* character, const RE::NiMatrix3& rotation,
            const RE::NiPoint3* position, float weight)
        {
            const auto parent = Skeleton::Relative(bone.parent, character);
            const auto toParent = parent.rotate.Transpose();
            bone.local.rotate = Skeleton::Slerp(bone.local.rotate, toParent * rotation, weight);
            if (position && parent.scale > 0.0F) {
                const auto local = (toParent * (*position - parent.translate)) / parent.scale;
                bone.local.translate = bone.local.translate + (local - bone.local.translate) * weight;
            }
        }

        // Moves bone by offset, relative to the character.
        void Nudge(RE::NiAVObject& bone, const RE::NiAVObject* character, const RE::NiPoint3& offset)
        {
            const auto parent = Skeleton::Relative(bone.parent, character);
            if (parent.scale > 0.0F) {
                bone.local.translate += (parent.rotate.Transpose() * offset) / parent.scale;
            }
        }

        // Poses the body; returns where it put each ski (units right of the character) if it did.
        std::optional<std::array<float, 2>> Apply(RE::PlayerCharacter* player, const Skiing::Body& body, float delta)
        {
            const float target = body.active && Settings::StanceEnabled() ? 1.0F : 0.0F;
            const float step = std::max(0.0F, delta) / kBlendSeconds;
            g_weight = target > g_weight ? std::min(target, g_weight + step) : std::max(target, g_weight - step);
            if (g_weight <= 0.0F) {
                return std::nullopt;
            }
            auto* character = player->Get3D(false);
            if (!character) {
                return std::nullopt;
            }
            if (g_live.root != character) {
                Find(character);
            }
            std::scoped_lock lock(g_standingLock);
            if (!g_live.complete || !g_standing.valid) {
                return std::nullopt;
            }
            const auto& standing = g_standing.pose;
            const auto  settings = Settings::Stance();
            const auto  angles = Core::Stance(settings, body.tuck);
            const auto  turn = Core::LeanFor(settings, body.turnAcceleration, body.gravity, body.edgeRadians);
            const float w = g_weight;
            auto        bone = [&](Bone b) -> RE::NiAVObject& { return *g_live.bones[b]; };

            // Skis lie on the ground under where the feet stand, and the ankles sit on them.
            const float heading = player->data.angle.z;
            const auto  normal = Skeleton::ToCharacter(body.groundNormal, heading);
            const float yaw = std::remainder(body.yaw - heading, 2.0F * std::numbers::pi_v<float>);
            std::array<float, 2>         stance{};
            std::array<RE::NiPoint3, 2>  ankles;
            std::array<RE::NiMatrix3, 2> onSki;
            for (int side = 0; side < 2; ++side) {
                const auto& foot = standing[kFoot[side]].translate;
                const auto  ski = Core::PlaceSki(normal, yaw, body.edgeRadians, foot.x, Skis::kHalfWidth);
                stance[side] = foot.x;
                onSki[side] = Skeleton::FromAxes(ski.right, ski.forward, ski.up);
                ankles[side] = ToPoint(ski.position) + ToPoint(ski.forward) * foot.y + ToPoint(ski.up) * foot.z;
            }

            // Hips: lowered and moved back so the bent legs keep the feet under them, then swung about the
            // feet into the turn.
            const auto   shift = Core::HipShiftFor(g_standing.thigh, g_standing.shin, angles.hip, angles.ankle);
            const auto   hipsRoll = Roll(turn.hips);
            const auto&  leftFoot = standing[kLeftFoot].translate;
            const auto&  rightFoot = standing[kRightFoot].translate;
            const RE::NiPoint3 pivot{ (leftFoot.x + rightFoot.x) * 0.5F, (leftFoot.y + rightFoot.y) * 0.5F, 0.0F };
            RE::NiPoint3 hips = standing[kCom].translate + RE::NiPoint3{ 0.0F, shift.forward, shift.up };
            hips = pivot + hipsRoll * (hips - pivot);
            Set(bone(kCom), character, hipsRoll * standing[kCom].rotate, &hips, w);
            Set(bone(kPelvis), character, hipsRoll * Pitch(-angles.torso * kPelvisLean) * standing[kPelvis].rotate, nullptr, w);

            // Legs: each reaches its own ski, the knees bending forward. If either can't, the hips come down.
            auto solve = [&](int side) {
                const auto hip = Skeleton::Relative(&bone(kThigh[side]), character).translate;
                return Core::SolveLeg(ToVec(hip), ToVec(ankles[side]), g_standing.thigh, g_standing.shin, ToVec(kForward));
            };
            const float excess = std::max(solve(0).excess, solve(1).excess);
            if (excess > 0.0F) {
                Nudge(bone(kCom), character, { 0.0F, 0.0F, -excess });
            }
            for (int side = 0; side < 2; ++side) {
                const auto leg = solve(side);
                const auto hip = Skeleton::Relative(&bone(kThigh[side]), character).translate;
                const auto hinge = ToPoint(leg.hinge);
                const auto& thigh = standing[kThigh[side]];
                const auto& calf = standing[kCalf[side]];
                const auto& foot = standing[kFoot[side]];
                const auto thighTurn = Turn(calf.translate - thigh.translate, kRight, ToPoint(leg.knee) - hip, hinge);
                const auto calfTurn = Turn(foot.translate - calf.translate, kRight, ToPoint(leg.ankle) - ToPoint(leg.knee), hinge);
                Set(bone(kThigh[side]), character, thighTurn * thigh.rotate, nullptr, w);
                Set(bone(kCalf[side]), character, calfTurn * calf.rotate, nullptr, w);
                Set(bone(kFoot[side]), character, onSki[side] * foot.rotate, nullptr, w);
            }

            const Bone spine[3] = { kSpine, kSpine1, kSpine2 };
            for (int i = 0; i < 3; ++i) {
                const auto roll = Roll(turn.hips + (turn.upper - turn.hips) * kSpineRoll[i]);
                Set(bone(spine[i]), character, roll * Pitch(-angles.torso * kSpineLean[i]) * standing[spine[i]].rotate, nullptr, w);
            }
            Set(bone(kNeck), character, Roll(turn.upper * kNeckRoll) * Pitch(-angles.torso * kNeckLean) * standing[kNeck].rotate,
                nullptr, w);

            // Arms: aimed at directions relative to the leaning torso, whatever pose the skeleton stands in.
            const auto lean = Roll(turn.upper) * Pitch(-angles.torso);
            for (int side = 0; side < 2; ++side) {
                const float        outward = side == 0 ? -1.0F : 1.0F;
                const RE::NiPoint3 hanging{ outward * std::sin(angles.armOut), 0.0F, -std::cos(angles.armOut) };
                const auto upperArm = lean * Pitch(angles.armForward) * hanging;
                const auto forearm = lean * Pitch(angles.armForward + angles.elbow) * hanging;

                const auto& upper = standing[kUpperArm[side]];
                const auto& fore = standing[kForearm[side]];
                const auto& hand = standing[kHand[side]];
                const auto  upperTurn = Skeleton::Between(fore.translate - upper.translate, upperArm);
                const auto  foreTurn = Skeleton::Between(upperTurn * (hand.translate - fore.translate), forearm) * upperTurn;

                Set(bone(kClavicle[side]), character, lean * standing[kClavicle[side]].rotate, nullptr, w);
                Set(bone(kUpperArm[side]), character, upperTurn * upper.rotate, nullptr, w);
                Set(bone(kForearm[side]), character, foreTurn * fore.rotate, nullptr, w);
                Set(bone(kHand[side]), character, foreTurn * hand.rotate, nullptr, w);
            }

            return stance;
        }

        struct UpdateAnimation
        {
            static void thunk(RE::PlayerCharacter* player, float delta)
            {
                func(player, delta);
                const auto body = Skiing::GetBody();
                const auto stance = Apply(player, body, delta);
                Skis::Place(player, body, delta, stance ? &*stance : nullptr);
            }

            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    void Install()
    {
        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PlayerCharacter[0] };
        UpdateAnimation::func = vtable.write_vfunc(0x7D, UpdateAnimation::thunk);
        spdlog::info("hooked PlayerCharacter::UpdateAnimation (0x7D)");
    }

    void Prepare(RE::PlayerCharacter* player)
    {
        auto* race = player->GetRace();
        auto* base = player->GetActorBase();
        if (!race || !base) {
            return;
        }
        const auto sex = base->GetSex() == RE::SEX::kFemale ? 1 : 0;
        const std::string path = race->skeletonModels[sex].GetModel();
        {
            std::scoped_lock lock(g_standingLock);
            if (g_standing.skeleton == path) {
                return;
            }
        }

        Standing standing{ .skeleton = path };
        const auto model = path.empty() ? nullptr : Skeleton::LoadModel(path);
        std::string missing;
        for (std::size_t i = 0; i < kBoneCount; ++i) {
            const auto* node = model ? model->GetObjectByName(kBoneNames[i]) : nullptr;
            if (node) {
                standing.pose[i] = Skeleton::Relative(node, model.get());
            } else {
                missing += missing.empty() ? kBoneNames[i] : std::string(", ") + kBoneNames[i];
            }
        }
        if (!model) {
            spdlog::warn("ski stance: could not load skeleton '{}'", path);
        } else if (!missing.empty()) {
            spdlog::warn("ski stance: skeleton '{}' has no {}", path, missing);
        } else {
            const auto& pose = standing.pose;
            standing.thigh = (pose[kLeftCalf].translate - pose[kLeftThigh].translate).Length();
            standing.shin = (pose[kLeftFoot].translate - pose[kLeftCalf].translate).Length();
            standing.valid = standing.thigh > 1.0F && standing.shin > 1.0F;
            const auto arm = pose[kLeftForearm].translate - pose[kLeftUpperArm].translate;
            spdlog::info("ski stance: skeleton '{}': thigh {:.1f}, shin {:.1f} units; left upper arm points "
                         "({:+.2f}, {:+.2f}, {:+.2f})",
                path, standing.thigh, standing.shin, arm.x / arm.Length(), arm.y / arm.Length(), arm.z / arm.Length());
        }
        std::scoped_lock lock(g_standingLock);
        g_standing = std::move(standing);
    }
}
