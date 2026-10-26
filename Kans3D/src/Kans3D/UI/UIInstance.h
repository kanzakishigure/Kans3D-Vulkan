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
        size_t   TreeOrder = 0;
        // Instance IDs prefix the source widget ID, so repeated reusable widgets have independent state.
        std::string           Key, Path;
        glm::vec2             Size {0};
        glm::mat4             Transform {1}; // Widget local Y-down pixels -> canvas pixels.
        glm::vec4             Bounds {0}, Clip {0};
        bool                  Visible = true, Enabled = true, LayoutDirty = true;
        bool                  RectDirty = true, MeasureDirty = true, GroupDirty = true;
        bool                  Driven = false;
        glm::vec2             MeasureAvailable {-1};
        glm::vec2             Preferred {0}, LayoutPosition {0}, LayoutSize {0};
        glm::vec4             Tint {1};
        AssetID               VisualTexture;
        bool                  HasVisualTexture = false;
        std::string           VisualState;
        glm::vec4             FadeFrom {1}, FadeTo {1};
        float                 FadeTime = 0;
        uint32_t              Caret = 0, Anchor = 0;
        std::string           EditOriginal, Composition; // Reserved for a later IME composition pipeline.
        std::vector<uint32_t> EditBoundaries;            // Display caret index -> original UTF-8 byte boundary.
        glm::vec2  TextScroll {0}, ScrollOffset {0}, ScrollVelocity {0}, ElasticOffset {0}, ExternalOffset {0};
        float      CaretTime = 0;
        bool       Editing = false, CaretVisible = false;
        float      Opacity = 1;
        TextLayout Text;
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
        void                   Tick(float              seconds,
                                    const std::string& hovered = {},
                                    const std::string& pressed = {},
                                    const std::string& focused = {});
        bool                   SetValue(const std::string& path, const YAML::Node& value);
        YAML::Node             Value(const std::string& path) const;
        bool                   AddChild(const std::string& parent, UIWidget);
        bool                   Remove(const std::string& path);
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
            size_t RectsResolved = 0, TextsLaidOut = 0, LayoutPasses = 0, MeasurePasses = 0;
        };
        const Statistics& Stats() const { return m_Stats; }

    private:
        friend class UIContext;
        void               Invalidate(const std::string& path, bool geometry = true);
        void               Measure(UIRuntimeWidget&, glm::vec2 available, FontSystem*, const FontResolver&);
        void               Arrange(UIRuntimeWidget&);
        bool               ApplyProperty(const std::string&, const std::string&, const YAML::Node&);
        UIRuntimeWidget*   MutableFind(const std::string&);
        AssetID            m_ID;
        Ref<const UIAsset> m_Asset;
        UIAssetResolver    m_Resolver;
        std::map<AssetID, Ref<const UIAsset>>                    m_Sources;
        std::vector<UIRuntimeWidget>                             m_Widgets;
        UIRuntimeWidget                                          m_CanvasRoot;
        bool                                                     m_CustomCanvasRect = false;
        std::map<std::string, std::map<std::string, YAML::Node>> m_Changes;
        glm::vec2                                                m_Canvas {-1};
        float                                                    m_PixelScale = 1;
        bool                                                     m_Dirty      = true;
        uint64_t                                                 m_Revision   = 0;
        Statistics                                               m_Stats;
    };
} // namespace Kans
