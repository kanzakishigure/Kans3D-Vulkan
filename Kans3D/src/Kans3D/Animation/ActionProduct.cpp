#include "kspch.h"
#include <cstring>
#include <set>
#include <stdexcept>
#include "AnimationProduct.h"
namespace Kans
{
    AnimationValidation ValidateAction(const ActionData& a, const AnimationClipData* clip)
    {
        auto error = [](std::string text) -> AnimationValidation {
            return AssetError {AssetErrorCode::InvalidData, "Action: " + text};
        };
        auto text = [](const std::string& s) { return s.size() <= 4096 && s.find('\0') == std::string::npos; };
        if (!a.Clip.IsValid())
            return error("unknown clip (invalid asset ID)");
        if (!text(a.Slot) || !std::isfinite(a.BlendIn) || a.BlendIn < 0 || !std::isfinite(a.BlendOut) ||
            a.BlendOut < 0 || !std::isfinite(a.Speed) || a.Speed <= 0)
            return error("invalid slot, blend duration or speed (must be positive)");
        if (a.Sections.size() > 4096 || a.Events.size() > 65536)
            return error("too many sections/events");
        if (clip && (!std::isfinite(clip->Duration) || clip->Duration <= 0))
            return error("clip has no duration");
        std::set<std::string> names;
        for (const auto& s : a.Sections)
        {
            if (s.Name.empty() || !text(s.Name) || !text(s.Next) || !names.insert(s.Name).second)
                return error("invalid or duplicate section: " + s.Name);
            if (!std::isfinite(s.Start) || !std::isfinite(s.End) || s.Start < 0 || s.End <= s.Start)
                return error("section End must be greater than Start: " + s.Name);
            if (clip && s.End > clip->Duration)
                return error("section exceeds clip: " + s.Name);
        }
        for (const auto& s : a.Sections)
            if (!s.Next.empty() && !names.count(s.Next))
                return error("unknown Next section: " + s.Next);
        double previous = -1;
        for (const auto& e : a.Events)
        {
            if (!text(e.Name) || e.Name.empty() || !text(e.Payload) || !std::isfinite(e.Time) || e.Time < 0 ||
                e.Time < previous || (clip && e.Time > clip->Duration))
                return error("invalid or unsorted event: " + e.Name);
            previous = e.Time;
        }
        return std::monostate {};
    }
    namespace
    {
        void Valid(const ActionData& a)
        {
            if (auto v = ValidateAction(a); auto e = std::get_if<AssetError>(&v))
                throw std::runtime_error(e->Message);
        }
    } // namespace
    YAML::Node EncodeActionNode(const ActionData& a)
    {
        Valid(a);
        YAML::Node n;
        n["Clip"]       = a.Clip.ToString();
        n["Slot"]       = a.Slot;
        n["BlendIn"]    = a.BlendIn;
        n["BlendOut"]   = a.BlendOut;
        n["Speed"]      = a.Speed;
        n["RootMotion"] = a.RootMotion;
        n["Sections"]   = YAML::Node(YAML::NodeType::Sequence);
        for (const auto& s : a.Sections)
        {
            YAML::Node v;
            v["Name"]  = s.Name;
            v["Start"] = s.Start;
            v["End"]   = s.End;
            v["Next"]  = s.Next;
            n["Sections"].push_back(v);
        }
        n["Events"] = YAML::Node(YAML::NodeType::Sequence);
        for (const auto& e : a.Events)
        {
            YAML::Node v;
            v["Name"]    = e.Name;
            v["Time"]    = e.Time;
            v["Payload"] = e.Payload;
            n["Events"].push_back(v);
        }
        return n;
    }
    ActionData DecodeActionNode(const YAML::Node& n)
    {
        if (!n.IsMap())
            throw std::runtime_error("Action must be a YAML map");
        ActionData a;
        const auto id = n["Clip"].as<std::string>();
        a.Clip        = AssetID::FromString(id);
        if (id != a.Clip.ToString())
            throw std::runtime_error("Action: invalid Clip asset ID");
        if (n["Slot"])
            a.Slot = n["Slot"].as<std::string>();
        if (n["BlendIn"])
            a.BlendIn = n["BlendIn"].as<double>();
        if (n["BlendOut"])
            a.BlendOut = n["BlendOut"].as<double>();
        if (n["Speed"])
            a.Speed = n["Speed"].as<double>();
        if (n["RootMotion"])
            a.RootMotion = n["RootMotion"].as<bool>();
        for (const auto* key : {"Sections", "Events"})
            if (n[key] && (!n[key].IsSequence() || n[key].size() > 65536))
                throw std::runtime_error(std::string("Action: invalid ") + key);
        if (n["Sections"])
            for (const auto& s : n["Sections"])
                a.Sections.push_back({s["Name"].as<std::string>(),
                                      s["Start"].as<double>(),
                                      s["End"].as<double>(),
                                      s["Next"] ? s["Next"].as<std::string>() : ""});
        if (n["Events"])
            for (const auto& e : n["Events"])
                a.Events.push_back({e["Time"].as<double>(),
                                    e["Name"].as<std::string>(),
                                    e["Payload"] ? e["Payload"].as<std::string>() : ""});
        std::stable_sort(
            a.Events.begin(), a.Events.end(), [](const auto& x, const auto& y) { return x.Time < y.Time; });
        Valid(a);
        return a;
    }
    AnimationProductBytes EncodeActionProduct(const ActionData& a)
    {
        try
        {
            YAML::Node root;
            root["magic"]   = "KACN";
            root["version"] = 1;
            root["data"]    = EncodeActionNode(a);
            YAML::Emitter out;
            out.SetDoublePrecision(17);
            out << root;
            if (!out.good() || out.size() > 16 * 1024 * 1024)
                throw std::runtime_error("Action Product too large");
            std::vector<std::byte> bytes(out.size());
            std::memcpy(bytes.data(), out.c_str(), out.size());
            return bytes;
        }
        catch (const std::exception& e)
        {
            return AssetError {AssetErrorCode::InvalidData, e.what()};
        }
    }
    std::variant<ActionData, AssetError> DecodeActionProduct(const std::byte* bytes, size_t size)
    {
        try
        {
            if (!bytes || !size || size > 16 * 1024 * 1024)
                throw std::runtime_error("Invalid Action Product size");
            auto docs = YAML::LoadAll(std::string(reinterpret_cast<const char*>(bytes), size));
            if (docs.size() != 1)
                throw std::runtime_error("Action Product requires one document");
            const auto n = docs[0];
            if (!n.IsMap() || n.size() != 3 || n["magic"].as<std::string>() != "KACN" || n["version"].as<int>() != 1)
                throw std::runtime_error("Invalid Action Product header");
            return DecodeActionNode(n["data"]);
        }
        catch (const std::exception& e)
        {
            return AssetError {AssetErrorCode::InvalidData, e.what()};
        }
    }
} // namespace Kans
