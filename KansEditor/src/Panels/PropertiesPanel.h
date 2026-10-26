#pragma once
#include <any>
#include <array>
#include <filesystem>
#include <functional>
#include <typeindex>
#include <vector>
#include "Kans3D/Asset/Manager/AssetManager.h"
#include "Kans3D/Editor/EditorAssetChoice.h"
#include "Kans3D/Renderer/RenderView.h"
#include "Kans3D/Scene/Components.h"
#include "Components/ComponentRegistry.h"
#include "HierarchyPanel.h"

namespace Kans
{
    // Edits the components of the entity selected in the hierarchy: a header
    // with the entity name, one collapsible section per component (generated from
    // the ComponentRegistry, with Reset, Copy/Paste, Revert and Remove) and an
    // Add Component menu grouped by category.
    class PropertiesPanel final : public ComponentUI
    {
    public:
        explicit PropertiesPanel(HierarchyPanel& hierarchy) : m_Hierarchy(hierarchy) {}

        void Draw();
        // Project-relative map the editor lights scenes without environment settings with.
        void SetDefaultEnvironment(std::filesystem::path map) { m_DefaultEnvironment = std::move(map); }
        void SetAssetChoices(std::vector<EditorAssetChoice> choices) { m_AssetChoices = std::move(choices); }
        // Used to show model parts and material names; may be null.
        void SetAssetManager(AssetManager* assets) { m_Assets = assets; }
        // Material previews (as the Content Browser shows them) and its selected files; set by the editor.
        using ThumbnailProvider = std::function<Ref<Texture2D>(const std::filesystem::path&, uint32_t size)>;
        void SetMaterialThumbnailProvider(ThumbnailProvider provider) { m_Thumbnail = std::move(provider); }
        void SetBrowserSelection(std::function<std::vector<std::filesystem::path>()> selection)
        {
            m_BrowserSelection = std::move(selection);
        }
        // Material actions of the slot rows: open a .material file, show a file in the Content
        // Browser, and create an instance of a material (its ID and source file) for some slots.
        struct MaterialActions
        {
            std::function<void(const std::filesystem::path&)>                            Open, Reveal;
            std::function<void(AssetID, const std::filesystem::path&, const SlotParts&)> CreateInstance;
        };
        void SetMaterialActions(MaterialActions actions) { m_MaterialActions = std::move(actions); }
        // Adds the slot rows' hovered, highlighted and isolated parts to the viewport's highlights.
        void AppendSlotHighlights(ViewHighlights& highlights) const;

        // ComponentUI
        Scene*        GetScene() override { return m_Hierarchy.GetScene(); }
        AssetManager* GetAssets() override { return m_Assets; }
        void          Select(Entity entity) override { m_Hierarchy.SetSelected(entity); }
        bool          AssetPicker(const char* label, AssetType type, AssetID& id) override;
        bool        MaterialPicker(const char* label, AssetID imported, AssetID current, AssetID& chosen) override;
        std::string MaterialName(AssetID material) override;
        bool           MaterialCombo(AssetID imported, AssetID current, AssetID& chosen) override;
        Ref<Texture2D> MaterialThumbnail(AssetID material) override;
        AssetID        BrowserMaterial() override;
        AssetID        AcceptMaterialDrop() override;
        void           HoverSlot(const SlotParts& parts) override { m_HoveredSlot = parts; }
        bool           IsSlotHighlighted(const SlotParts& parts) override;
        void           ToggleSlotHighlight(const SlotParts& parts) override;
        bool IsSlotIsolated(const SlotParts& parts) override { return !parts.empty() && m_IsolatedSlot == parts; }
        void ToggleSlotIsolate(const SlotParts& parts) override;
        bool           CanOpenMaterial(AssetID material) override;
        void           OpenMaterial(AssetID material) override;
        void           RevealAsset(AssetID asset) override;
        void           CreateMaterialInstance(AssetID parent, const SlotParts& parts) override;

    private:
        // Scene-wide settings, shown when no entity is selected.
        void DrawContents();
        void DrawSearch();
        void DrawSceneSettings(Scene& scene);
        void DrawEnvironmentGroups(Scene& scene);
        void DrawPostProcess(Scene& scene);
        void DrawHeader(Entity entity);
        void DrawPrefabPart(Entity entity);
        void DrawSection(Entity entity, const ComponentEntry& component);
        void DrawAddComponent(Entity entity);
        // The published asset of `type` whose source is `file`, or invalid.
        AssetID ChoiceFor(const std::filesystem::path& file, AssetType type) const;
        // The source file an asset was imported from, or empty.
        std::filesystem::path SourceOf(AssetID asset) const;

        HierarchyPanel&                m_Hierarchy;
        std::vector<EditorAssetChoice> m_AssetChoices;
        AssetManager*                  m_Assets = nullptr;
        std::array<char, 128>          m_ComponentFilter {};
        std::string                    m_Search;
        bool                           m_ModifiedOnly = false;
        std::array<char, 128>          m_AssetFilter {};
        // Copied component values (Copy/Paste in a section's menu).
        std::type_index m_ClipboardType = typeid(void);
        std::any        m_Clipboard;
        std::vector<std::filesystem::path> m_EnvironmentMaps;
        std::filesystem::path              m_DefaultEnvironment;
        ThumbnailProvider                                   m_Thumbnail;
        std::function<std::vector<std::filesystem::path>()> m_BrowserSelection;
        MaterialActions                                     m_MaterialActions;
        // Slot rows' viewport feedback; pinned and isolated rows reset when the selection changes.
        SlotParts              m_HoveredSlot, m_IsolatedSlot;
        std::vector<SlotParts> m_HighlightedSlots;
        UUID                   m_SlotsOwner = 0;
    };
} // namespace Kans
