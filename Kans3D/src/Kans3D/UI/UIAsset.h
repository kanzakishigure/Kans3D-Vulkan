#pragma once
#include <map>
#include <variant>
#include <yaml-cpp/yaml.h>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetError.h"
#include "Kans3D/Text/TextComponent.h"

namespace Kans
{
    enum class UIWidgetType
    {
        Panel,
        Image,
        Text,
        UIInstance
    };
    enum class UIScaleMode
    {
        ConstantPixelSize,
        ScaleWithScreenSize
    };
    struct UIRect
    {
        glm::vec2 AnchorsMin {0}, AnchorsMax {0}, Pivot {.5f};
        glm::vec2 Position {0}, Size {100}, Scale {1};
        glm::vec4 Offsets {0}; // Left, top, right, bottom. Positive offsets inset stretched edges.
        float     Rotation = 0;
    };
    inline bool operator==(const UIRect& a, const UIRect& b)
    {
        return a.AnchorsMin == b.AnchorsMin && a.AnchorsMax == b.AnchorsMax && a.Pivot == b.Pivot &&
               a.Position == b.Position && a.Size == b.Size && a.Scale == b.Scale && a.Offsets == b.Offsets &&
               a.Rotation == b.Rotation;
    }
    struct UIWidget
    {
        uint64_t     ID = 0;
        std::string  Name;
        UIWidgetType Type    = UIWidgetType::Panel;
        bool         Visible = true, Enabled = true;
        UIRect       Rect;
        int          SortOrder = 0;
        // Extensible YAML property bag. Unknown properties survive source/product round trips.
        YAML::Node            Properties {YAML::NodeType::Map};
        std::vector<UIWidget> Children;
        UIWidget() = default;
        UIWidget(const UIWidget&);
        UIWidget& operator=(const UIWidget&);
        UIWidget(UIWidget&&) noexcept            = default;
        UIWidget& operator=(UIWidget&&) noexcept = default;
    };
    struct UIExposedProperty
    {
        std::string Name;
        uint64_t    WidgetID = 0;
        std::string Property;
    };
    struct UIData
    {
        glm::vec2                      ReferenceResolution {1920, 1080};
        UIScaleMode                    ScaleMode = UIScaleMode::ConstantPixelSize;
        float                          Match     = .5f;
        std::vector<UIWidget>          Widgets;
        std::vector<UIExposedProperty> Exposed;
    };
    class UIAsset final : public Asset
    {
    public:
        explicit UIAsset(UIData data) : m_Data(std::move(data)) {}
        static AssetType GetStaticType() { return AssetType::UI; }
        AssetType        GetAssetType() const override { return GetStaticType(); }
        const UIData&    GetData() const { return m_Data; }

    private:
        UIData m_Data;
    };
    template<class T>
    T UIProperty(const UIWidget& w, const std::string& name, T fallback)
    {
        auto n = w.Properties[name];
        return n ? n.as<T>() : fallback;
    }
    glm::vec2                                UIVec2(const YAML::Node&, glm::vec2 fallback = {});
    glm::vec4                                UIVec4(const YAML::Node&, glm::vec4 fallback = {});
    YAML::Node                               UIEncode(glm::vec2);
    YAML::Node                               UIEncode(glm::vec4);
    YAML::Node                               UIEncode(const UIRect&);
    UIRect                                   UIDecodeRect(const YAML::Node&);
    std::variant<UIData, AssetError>         ParseUISource(const std::byte*, size_t);
    std::variant<std::string, AssetError>    SerializeUISource(const UIData&);
    std::variant<std::monostate, AssetError> ValidateUI(const UIData&);
    std::map<AssetID, AssetType>             UIDependencies(const UIData&);
    // Applies a typed property (or common Visible/Enabled/Rect); reports invalid names/values.
    std::variant<std::monostate, AssetError> UISetProperty(UIWidget&, const std::string&, const YAML::Node&);
    TextLayoutRequest                        UITextRequest(const UIWidget&, glm::vec2 size, float pixelScale);
    TextComponent                            UITextStyle(const UIWidget&, float pixelScale);
} // namespace Kans
