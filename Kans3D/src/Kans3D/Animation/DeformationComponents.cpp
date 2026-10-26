#include "kspch.h"
#include "DeformationComponents.h"
namespace Kans
{
    namespace
    {
        bool Same(const IKTarget& a, const IKTarget& b)
        {
            return a.Entity == b.Entity && a.Position == b.Position && a.Rotation == b.Rotation;
        }
        bool Same(const TwoBoneIK& a, const TwoBoneIK& b)
        {
            return a.Root == b.Root && a.Middle == b.Middle && a.End == b.End && Same(a.Target, b.Target) &&
                   a.PoleEntity == b.PoleEntity && a.PoleDirection == b.PoleDirection && a.Weight == b.Weight &&
                   a.RotationWeight == b.RotationWeight && a.Stretch == b.Stretch;
        }
        bool Same(const LookAtIK& a, const LookAtIK& b)
        {
            if (!Same(a.Target, b.Target) || a.WorldUp != b.WorldUp || a.Weight != b.Weight ||
                a.AngleLimit != b.AngleLimit || a.Bones.size() != b.Bones.size())
                return false;
            for (size_t i = 0; i < a.Bones.size(); ++i)
            {
                const auto& x = a.Bones[i];
                const auto& y = b.Bones[i];
                if (x.Joint != y.Joint || x.Weight != y.Weight || x.Forward != y.Forward || x.Up != y.Up)
                    return false;
            }
            return true;
        }
        bool Same(const std::vector<IKConstraint>& a, const std::vector<IKConstraint>& b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
            {
                const auto& x = a[i];
                const auto& y = b[i];
                if (x.Name != y.Name || x.Enabled != y.Enabled || x.Weight != y.Weight ||
                    x.Data.index() != y.Data.index())
                    return false;
                if (!std::visit(
                        [&](const auto& data) { return Same(data, std::get<std::decay_t<decltype(data)>>(y.Data)); },
                        x.Data))
                    return false;
            }
            return true;
        }
    } // namespace
    struct IKRuntime
    {
        const SkeletonData*         Skeleton = nullptr;
        std::vector<IKConstraint>   Constraints;
        std::vector<Animator>       Solvers;
        std::vector<JointTransform> Before;
    };
    IKComponent::IKComponent(const IKComponent& c) :
        Constraints(c.Constraints), Skeleton(c.Skeleton), SkeletonSource(c.SkeletonSource)
    {}
    IKComponent& IKComponent::operator=(const IKComponent& c)
    {
        if (this != &c)
        {
            IKComponent copy(c);
            *this = std::move(copy);
        }
        return *this;
    }
    bool operator==(const IKComponent& a, const IKComponent& b)
    {
        return a.Skeleton == b.Skeleton && Same(a.Constraints, b.Constraints);
    }
    bool operator==(const ShapeKeysComponent& a, const ShapeKeysComponent& b)
    {
        return a.Skeleton == b.Skeleton && a.Expression == b.Expression && a.Weights == b.Weights;
    }
    void IKComponent::ImportLegacy(const AnimationControllerData& c)
    {
        for (auto ik : c.TwoBoneConstraints)
        {
            Constraints.push_back({ik.Name, true, 1, ik});
        }
        for (auto ik : c.LookAtConstraints)
        {
            Constraints.push_back({ik.Name, true, 1, ik});
        }
    }
    void IKComponent::RemapEntities(const std::function<uint64_t(uint64_t)>& remap)
    {
        for (auto& c : Constraints)
            std::visit(
                [&](auto& ik) {
                    if (ik.Target.Entity)
                        ik.Target.Entity = remap(ik.Target.Entity);
                    if constexpr (std::is_same_v<std::decay_t<decltype(ik)>, TwoBoneIK>)
                        if (ik.PoleEntity)
                            ik.PoleEntity = remap(ik.PoleEntity);
                },
                c.Data);
    }
    bool ShapeKeysComponent::ApplyPreset(const std::string& name, float weight)
    {
        if (!ExpressionSource || !std::isfinite(weight) || weight < 0 || weight > 1)
            return false;
        for (const auto& preset : ExpressionSource->GetData().Presets)
            if (preset.Name == name)
            {
                for (const auto& key : preset.Weights)
                    Weights[key.Name] = key.Weight * weight;
                return true;
            }
        return false;
    }
    void EvaluateIKConstraints(const SkeletonData&               skeleton,
                               const IKComponent&                component,
                               AnimationPose&                    pose,
                               const glm::mat4&                  world,
                               const Animator::IKEntityResolver& resolver)
    {
        if (!component.Runtime)
            component.Runtime = CreateRef<IKRuntime>();
        auto& runtime = *component.Runtime;
        if (runtime.Skeleton != &skeleton || !Same(runtime.Constraints, component.Constraints))
        {
            runtime.Skeleton    = &skeleton;
            runtime.Constraints = component.Constraints;
            runtime.Solvers.clear();
            runtime.Solvers.resize(component.Constraints.size());
            runtime.Before.resize(skeleton.Joints.size());
            for (size_t i = 0; i < component.Constraints.size(); ++i)
            {
                const auto& constraint = component.Constraints[i];
                auto&       solver     = runtime.Solvers[i];
                solver.m_Skeleton      = &skeleton;
                AnimationControllerData data;
                std::visit(
                    [&](auto ik) {
                        if constexpr (std::is_same_v<decltype(ik), TwoBoneIK>)
                        {
                            data.TwoBoneConstraints.push_back(std::move(ik));
                        }
                        else
                            data.LookAtConstraints.push_back(std::move(ik));
                    },
                    constraint.Data);
                solver.m_Controller = &data;
                solver.PrepareIK();
                solver.m_Controller = nullptr;
            }
        }
        for (size_t i = 0; i < component.Constraints.size(); ++i)
        {
            const auto& c = component.Constraints[i];
            if (!c.Enabled || !std::isfinite(c.Weight) || c.Weight <= 0)
                continue;
            auto& solver = runtime.Solvers[i];
            solver.SetIKWorldTransform(world);
            solver.SetIKEntityResolver(resolver);
            const float weight = std::clamp(c.Weight, 0.f, 1.f);
            if (weight < 1)
                runtime.Before = pose.Local;
            solver.EvaluateIK(pose);
            if (weight < 1)
                for (size_t j = 0; j < pose.Local.size(); ++j)
                    pose.Local[j] = BlendJoint(runtime.Before[j], pose.Local[j], weight);
        }
        BuildModelPose(skeleton, pose);
    }
} // namespace Kans
