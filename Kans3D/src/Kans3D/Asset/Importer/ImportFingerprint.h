#pragma once
#include <map>
#include <variant>
#include <vector>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetError.h"
#include "LocalIDMap.h"

namespace Kans
{
    // Source files are identified by ID and content; moving a source with its
    // sidecar does not change the fingerprint.
    struct SourceFingerprintInput
    {
        SourceAssetID ID;
        std::string   ContentDigest;
    };
    struct ProductFingerprintInput
    {
        AssetID     ID;
        std::string ContentDigest;
    };
    struct ImportFingerprintInput
    {
        SourceFingerprintInput Source;
        // Stable importer/toolchain identities, never installation paths.
        std::string                        Importer;
        uint32_t                           ImporterVersion = 0;
        std::string                        Toolchain;
        std::string                        Target = "default";
        std::map<std::string, std::string> Settings;
        // Full successful identity history, including deleted-key reservations.
        // Use ImportResult.LocalIDs after allocation; cache checks use Store history.
        LocalIDMap LocalIDs;
        // Coordinator supplies the complete set of build inputs. These are not
        // runtime dependency scheduling and do not discover includes themselves.
        std::vector<SourceFingerprintInput>  Sources;
        std::vector<ProductFingerprintInput> Products;
    };

    // Versioned, deterministic SHA256 build key. No IO or mutable shared state.
    [[nodiscard]] std::variant<std::string, AssetError> ComputeImportFingerprint(const ImportFingerprintInput& input);
} // namespace Kans
