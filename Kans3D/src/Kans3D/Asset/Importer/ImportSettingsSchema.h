#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "Kans3D/Asset/Asset.h"

namespace Kans
{
    struct ImportSettingOption
    {
        std::string Value, Label, Description;
    };
    struct ImportSettingDesc
    {
        enum class Type
        {
            Bool,
            Int,
            Float,
            Enum,
            String,
            Asset
        };
        std::string                      Key;
        Type                             Kind = Type::String;
        std::string                      Default;
        std::vector<ImportSettingOption> Options; // Enum choices or suggested numeric values.
        std::optional<double>            Min, Max, Step;
        AssetType                        AssetKind = AssetType::None;
        std::string                      Label, Description, Group;
        bool                             Advanced = false;
        // A pattern has one trailing <placeholder>, matching a nonempty literal
        // suffix (including dots/punctuation). Substitute source names, except
        // <clip-key> uses inventory ObjectKey. <slot> uses the inventory slot
        // Key after its "material:" prefix (including the length and colon).
        // "inherit" for per-clip compression means use the global setting;
        // empty map/remap values and morph "auto" mean remove the override.
        bool        KeyPattern = false;
        std::string InheritFrom; // Optional global key for a pattern; Default is then "inherit".
    };
    struct ImportPreset
    {
        std::string                        Name, Description;
        std::map<std::string, std::string> Settings;
    };
    struct ImportSettingsSchema
    {
        std::vector<ImportSettingDesc> Settings;
        std::vector<ImportPreset>      Presets;
        const ImportSettingDesc*       Find(const std::string& key) const
        {
            for (const auto& setting : Settings)
            {
                if (!setting.KeyPattern && setting.Key == key)
                    return &setting;
                const auto at = setting.Key.find('<');
                if (setting.KeyPattern && at != std::string::npos && key.size() > at &&
                    key.compare(0, at, setting.Key, 0, at) == 0)
                    return &setting;
            }
            return nullptr;
        }
    };
} // namespace Kans
