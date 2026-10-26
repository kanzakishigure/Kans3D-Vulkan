#pragma once
#include <functional>
#include <unordered_map>
#include "UIAsset.h"
namespace Kans
{
    using UIAssetResolver = std::function<Ref<const UIAsset>(AssetID)>;
    struct UIRuntimeWidget
    {
        UIWidget Data;
        // Instance IDs prefix the source widget ID, so repeated reusable widgets have independent state.
        std::string          Key, Path;
        glm::vec2            Size {0};
        glm::mat4            Transform {1}; // Widget local Y-down pixels -> canvas pixels.
        glm::vec4            Bounds {0}, Clip {0};
        bool                 Visible = true, Enabled = true, LayoutDirty = true;
        float                Opacity = 1;
        TextLayout           Text;
        Ref<const FontAsset> Font;
        YAML::Node State {YAML::NodeType::Map}; // UI4/UI5 state survives rebuilds by source ID within instance scope.
        std::vector<UIRuntimeWidget> Children;
    };
    class UIInstance
    {
    public:
        explicit UIInstance(Ref<const UIAsset> asset = {}, UIAssetResolver resolver = {});
        UIInstance(AssetID asset, UIAssetResolver resolver);
        // Transactional rebuild on root/nested pointer changes. Last good tree remains usable on failure/loading.
        std::variant<bool, AssetError> Refresh();
        using FontResolver = std::function<Ref<const FontAsset>(AssetID)>;
        void                   Update(glm::vec2 canvasSize, FontSystem* fonts = nullptr, FontResolver fontsByID = {});
        bool                   SetText(const std::string& path, const std::string&);
        bool                   SetColor(const std::string& path, glm::vec4);
        bool                   SetVisible(const std::string& path, bool);
        bool                   SetTexture(const std::string& path, AssetID);
        bool                   SetRect(const std::string& path, const UIRect&);
        bool                   SetProperty(const std::string& path, const std::string&, const YAML::Node&);
        const UIRuntimeWidget* Find(const std::string& path) const;
        YAML::Node*            State(const std::string& path);
        const std::vector<UIRuntimeWidget>& Widgets() const { return m_Widgets; }
        const UIData*                       Data() const { return m_Asset ? &m_Asset->GetData() : nullptr; }
        float                               PixelScale() const { return m_PixelScale; }
        uint64_t                            Revision() const { return m_Revision; }
        struct Statistics
        {
            size_t RectsResolved = 0, TextsLaidOut = 0;
        };
        const Statistics& Stats() const { return m_Stats; }

    private:
        UIRuntimeWidget*                                         MutableFind(const std::string&);
        AssetID                                                  m_ID;
        Ref<const UIAsset>                                       m_Asset;
        UIAssetResolver                                          m_Resolver;
        std::map<AssetID, Ref<const UIAsset>>                    m_Sources;
        std::vector<UIRuntimeWidget>                             m_Widgets;
        std::map<std::string, std::map<std::string, YAML::Node>> m_Changes;
        glm::vec2                                                m_Canvas {-1};
        float                                                    m_PixelScale = 1;
        bool                                                     m_Dirty      = true;
        uint64_t                                                 m_Revision   = 0;
        Statistics                                               m_Stats;
    };
} // namespace Kans
