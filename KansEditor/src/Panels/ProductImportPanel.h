#pragma once
#include <array>
#include "Kans3D/Editor/EditorAssetImportService.h"

namespace Kans
{
    class ProductImportPanel
    {
    public:
        // Non-owning; editor disconnects before destroying the controller.
        void Bind(EditorAssetImportService* service);
        // Selects the source of `path`; the editor opens the window.
        void OpenWithFile(const std::filesystem::path& path);
        // Contents of the window the editor's panel manager opened.
        void Draw();

    private:
        void                                   ReloadSources();
        void                                   Select(const SourceAssetMetadata& source);
        void                                   DrawDeletion();
        // Material slots of a model source: remap each to a project material or extract them.
        void                                   DrawModelMaterials(bool busy);
        void                                   LoadSlots();
        // Persists a slot change, then rebuilds the model so the change shows.
        void                                   Rebuild();
        void                                   Record(const std::variant<std::monostate, AssetError>& result);
        EditorAssetImportService*              m_Service = nullptr;
        bool                                   m_Force   = false;
        SourceAssetID                          m_Selected;
        std::vector<SourceAssetMetadata>       m_Sources;
        SourceImportConfiguration              m_Configuration;
        std::string                            m_Error;
        std::string                            m_NewKey;
        std::string                            m_NewValue;
        std::string                            m_MoveDestination;
        std::optional<SourceAssetDeletionPlan> m_DeletionPlan;

        // Material slots of the selected model, reloaded after selection changes and builds.
        std::optional<std::vector<ModelMaterialSlot>> m_Slots;
        std::string                                   m_SlotsError;
        bool                                          m_SlotsStale = true;
        bool                                          m_WasBusy    = false;
        std::string                                   m_ExtractDirectory;
        std::vector<std::string>                      m_ExtractReport;
    };
} // namespace Kans
