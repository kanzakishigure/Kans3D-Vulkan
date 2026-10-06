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
        void Open() { m_Open = true; }
        void OpenWithFile(const std::filesystem::path& path);
        void Draw();

    private:
        void                                   ReloadSources();
        void                                   Select(const SourceAssetMetadata& source);
        void                                   DrawDeletion();
        void                                   Record(const std::variant<std::monostate, AssetError>& result);
        EditorAssetImportService*              m_Service = nullptr;
        bool                                   m_Open    = false;
        bool                                   m_Force   = false;
        SourceAssetID                          m_Selected;
        std::vector<SourceAssetMetadata>       m_Sources;
        SourceImportConfiguration              m_Configuration;
        std::string                            m_Error;
        std::string                            m_NewKey;
        std::string                            m_NewValue;
        std::string                            m_MoveDestination;
        std::optional<SourceAssetDeletionPlan> m_DeletionPlan;
    };
} // namespace Kans
