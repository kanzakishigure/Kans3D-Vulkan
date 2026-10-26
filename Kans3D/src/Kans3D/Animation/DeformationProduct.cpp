#include "kspch.h"
#include "DeformationProduct.h"
#include <stdexcept>
#include "AnimationProduct.h"
namespace Kans
{
    namespace
    {
        AnimationControllerData EmptyController()
        {
            AnimationControllerData c;
            c.Motions.emplace_back().InlineClip.emplace();
            c.Layers.emplace_back().States.push_back({"Rest", 0});
            return c;
        }
    } // namespace
    YAML::Node EncodeIKComponent(const IKComponent& component)
    {
        if (component.Constraints.size() > 512)
            throw std::runtime_error("Too many IK constraints");
        YAML::Node n;
        n["Skeleton"]    = component.Skeleton.ToString();
        n["Constraints"] = YAML::Node(YAML::NodeType::Sequence);
        for (const auto& constraint : component.Constraints)
        {
            if (constraint.Name.empty() || !std::isfinite(constraint.Weight) || constraint.Weight < 0 ||
                constraint.Weight > 1)
                throw std::runtime_error("Invalid IK name/weight");
            auto       c = EmptyController();
            YAML::Node v;
            v["Name"]    = constraint.Name;
            v["Enabled"] = constraint.Enabled;
            v["Weight"]  = constraint.Weight;
            std::visit(
                [&](auto ik) {
                    ik.Name = constraint.Name;
                    if constexpr (std::is_same_v<decltype(ik), TwoBoneIK>)
                    {
                        c.TwoBoneConstraints.push_back(ik);
                        v["Type"] = "TwoBone";
                        v["Data"] = EncodeAnimationControllerNode(c)["two_bone_ik"][0];
                    }
                    else
                    {
                        c.LookAtConstraints.push_back(ik);
                        v["Type"] = "LookAt";
                        v["Data"] = EncodeAnimationControllerNode(c)["look_at_ik"][0];
                    }
                },
                constraint.Data);
            n["Constraints"].push_back(v);
        }
        return n;
    }
    IKComponent DecodeIKComponent(const YAML::Node& n)
    {
        if (!n.IsMap() || !n["Constraints"].IsSequence() || n["Constraints"].size() > 512)
            throw std::runtime_error("Invalid IKComponent");
        IKComponent result;
        if (n["Skeleton"])
            result.Skeleton = AssetID::FromString(n["Skeleton"].as<std::string>());
        for (const auto& v : n["Constraints"])
        {
            auto       c    = EncodeAnimationControllerNode(EmptyController());
            const auto type = v["Type"].as<std::string>();
            if (type != "TwoBone" && type != "LookAt")
                throw std::runtime_error("Unknown IK constraint type");
            c[type == "TwoBone" ? "two_bone_ik" : "look_at_ik"].push_back(v["Data"]);
            const auto   decoded = DecodeAnimationControllerNode(c);
            IKConstraint constraint;
            constraint.Name    = v["Name"].as<std::string>();
            constraint.Enabled = v["Enabled"].as<bool>();
            constraint.Weight  = v["Weight"].as<float>();
            if (type == "TwoBone")
                constraint.Data = decoded.TwoBoneConstraints[0];
            else
                constraint.Data = decoded.LookAtConstraints[0];
            result.Constraints.push_back(std::move(constraint));
        }
        EncodeIKComponent(result);
        return result;
    }
} // namespace Kans
