#pragma once

#include "Kans3D/Editor/EditorPanel.h"
#include "Kans3D/Editor/EditorResources.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "ContentBrowserItem.h"

#include <vector>

namespace Kans
{
    class ContentBrowserPanel : public EditorPanel
    {
    public:
        // ── View modes: progressively richer layout as icon tier grows ──
        enum class ViewMode : uint8_t
        {
            CompactGrid,  // 48 / 64 px   →  icon + truncated name
            StandardGrid, // 80 / 96 px   →  icon + name + extension badge
            DetailedGrid, // 128 px       →  icon + name + ext + size + date
            ExpandedGrid  // 160 / 192 px →  icon + name + ext + size + full date
        };

    public:
        ContentBrowserPanel();
        virtual void onImGuiRender(bool isOpen) override;

        static ContentBrowserPanel* Get() { return s_Instance; }

        // ── Queries (called by ContentBrowserItem) ──
        float    GetCurrentIconSize() const { return m_CurrentIconSize; }
        ViewMode GetCurrentViewMode() const { return m_CurrentViewMode; }

    private:
        // Chooses an icon tier using the viewport and the grid dimensions.
        void CalculateLayout(float gridWidth, float gridHeight, float viewportWidth, float viewportHeight);
        void OnItemClicked(const std::filesystem::path& path, bool isDirectory, bool doubleClick);
        void SelectItem(const std::filesystem::path& path, bool isDirectory);
        void DrawPreview();

        std::vector<ContentBrowserItem> m_ContentBrowserItemList;

        std::filesystem::path m_CurrentPath;
        std::filesystem::path m_SelectedPath;
        Ref<Texture2D>        m_PreviewTexture;
        std::string           m_PreviewText;
        std::string           m_PreviewMessage;
        enum class PreviewType
        {
            None,
            Folder,
            Image,
            Text,
            Material,
            Other
        };
        PreviewType m_PreviewType   = PreviewType::None;
        ImVec4      m_MaterialColor = ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
        int         open_action     = -1;

        // Layout / styling
        float FramePadding    = 8.0f;
        float FrameBorderSize = 1.5f;
        float FrameRounding   = 8.0f;

        // ── Auto-scaling state ──
        float    m_CurrentIconSize = 96.0f;
        int      m_ComputedColumns = 4; // derived from layout calc
        ViewMode m_CurrentViewMode = ViewMode::StandardGrid;

        glm::vec2 OuterSize        = {0, 0};
        glm::vec2 ItemInnerSpacing = {36, 15};

        bool                        NeedRefresh = true;
        static ContentBrowserPanel* s_Instance;
        friend class ContentBrowserItem;
    };
} // namespace Kans
