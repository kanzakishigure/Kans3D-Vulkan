#pragma once
#include "ImporterRegistry.h"
namespace Kans
{
    // Staging phase only. A later transaction coordinator validates output files,
    // publishes the full Product set, then records successful import fingerprints.
    // The registry must outlive this coordinator. No global state or runtime loads.
    class AssetImporter
    {
    public:
        explicit AssetImporter(const ImporterRegistry& registry) : m_Registry(registry) {}
        [[nodiscard]] std::variant<ImportResult, AssetError> Import(const ImportContext& context) const;

    private:
        const ImporterRegistry& m_Registry;
    };
} // namespace Kans
