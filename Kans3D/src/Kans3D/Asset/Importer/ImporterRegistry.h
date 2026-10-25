#pragma once
#include <unordered_map>
#include "AssetImporterBase.h"

namespace Kans
{
    struct RegisteredImporter
    {
        // Stable name stored in .kmeta and in build fingerprints.
        std::string                              Name;
        std::shared_ptr<const AssetImporterBase> Importer;
        std::vector<std::string>                 Extensions;
    };

    // Maps source file extensions to named importers.
    class ImporterRegistry
    {
    public:
        // makeDefault selects this importer for its extensions when a source names
        // no importer; otherwise it is used only when named explicitly.
        [[nodiscard]] std::variant<std::monostate, AssetError> Register(const std::string&                 name,
                                                                        const std::vector<std::string>&    extensions,
                                                                        std::shared_ptr<AssetImporterBase> importer,
                                                                        bool makeDefault = true);
        // Selects the named importer, or the extension's default when name is empty.
        [[nodiscard]] std::variant<RegisteredImporter, AssetError> Resolve(const std::filesystem::path& sourceFile,
                                                                           const std::string& name = {}) const;
        std::variant<ImportSettingsSchema, AssetError> GetSettingsSchema(const std::string& importerName) const;
        std::variant<ImportSettingsSchema, AssetError>
        GetSettingsSchemaForSource(const std::filesystem::path& sourceFile, const std::string& importerName = {}) const;
        bool Supports(const std::filesystem::path& sourceFile) const;

    private:
        std::unordered_map<std::string, RegisteredImporter> m_Named;
        std::unordered_map<std::string, std::string>        m_Defaults; // extension -> name
    };

    // Model, Texture, Shader, Material and Audio importers.
    [[nodiscard]] std::variant<ImporterRegistry, AssetError> CreateDefaultImporterRegistry();
} // namespace Kans
