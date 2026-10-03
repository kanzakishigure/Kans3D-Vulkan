#pragma once
#include <unordered_map>
#include "AssetImporterBase.h"

namespace Kans
{
    // Project/tool-owned source format registry. Never keyed by runtime AssetType.
    class ImporterRegistry
    {
    public:
        [[nodiscard]] std::variant<std::monostate, AssetError> Register(const std::string&                 extension,
                                                                        std::shared_ptr<AssetImporterBase> importer);
        std::shared_ptr<const AssetImporterBase>               Find(const std::filesystem::path& sourceFile) const;

    private:
        std::unordered_map<std::string, std::shared_ptr<AssetImporterBase>> m_Importers;
    };
} // namespace Kans
