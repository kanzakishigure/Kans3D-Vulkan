#include "kspch.h"
#include "ImportFingerprint.h"
#include <algorithm>
#include <unordered_set>
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/ProductRecord.h"

namespace Kans
{
    namespace
    {
        constexpr size_t MaximumBytes        = 4 * 1024 * 1024;
        constexpr size_t MaximumString       = 64 * 1024;
        constexpr size_t MaximumSettings     = 4096;
        constexpr size_t MaximumDependencies = 65536;
        bool             ValidName(const std::string& value)
        {
            return !value.empty() && value.size() <= MaximumString && value.find('\0') == std::string::npos;
        }
        bool ValidSource(const SourceFingerprintInput& input)
        {
            return input.ID.IsValid() && IsContentDigest(input.ContentDigest);
        }
        class CanonicalKey
        {
        public:
            void Number(uint64_t value)
            {
                if (!Valid)
                    return;
                for (unsigned shift = 0; shift < 64; shift += 8)
                    Byte(static_cast<std::byte>((value >> shift) & 255));
            }
            void String(const std::string& value)
            {
                if (!Valid)
                    return;
                Number(value.size());
                if (value.size() > MaximumString || value.size() > MaximumBytes - Bytes.size())
                {
                    Valid = false;
                    return;
                }
                for (unsigned char valueByte : value)
                    Byte(static_cast<std::byte>(valueByte));
            }
            void Source(const SourceFingerprintInput& source)
            {
                if (!Valid)
                    return;
                Number(source.ID.GetUUID());
                String(source.ContentDigest);
            }
            void Byte(std::byte value)
            {
                if (Bytes.size() == MaximumBytes)
                    Valid = false;
                else
                    Bytes.push_back(value);
            }
            std::vector<std::byte> Bytes;
            bool                   Valid = true;
        };
    } // namespace

    std::variant<std::string, AssetError> ComputeImportFingerprint(const ImportFingerprintInput& input)
    {
        if (!ValidSource(input.Source) || !ValidName(input.Importer) || !input.ImporterVersion ||
            !ValidName(input.Toolchain) || !IsProductTarget(input.Target) || input.Settings.size() > MaximumSettings ||
            input.Sources.size() > MaximumDependencies || input.Products.size() > MaximumDependencies ||
            input.LocalIDs.size() > MaximumDependencies)
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid import fingerprint input or count limit"};
        for (const auto& [key, value] : input.Settings)
            if (!ValidName(key) || value.size() > MaximumString)
                return AssetError {AssetErrorCode::InvalidArgument, "Invalid import fingerprint setting"};
        for (const auto& source : input.Sources)
            if (!ValidSource(source) || source.ID == input.Source.ID)
                return AssetError {AssetErrorCode::InvalidArgument, "Invalid or self source fingerprint dependency"};
        for (const auto& product : input.Products)
            if (!product.ID.IsValid() || product.ID.GetSourceID() == input.Source.ID ||
                !IsContentDigest(product.ContentDigest))
                return AssetError {AssetErrorCode::InvalidArgument, "Invalid or self Product fingerprint dependency"};
        std::unordered_set<uint64_t> assigned;
        assigned.reserve(input.LocalIDs.size());
        for (const auto& [name, local] : input.LocalIDs)
        {
            if (name.empty() || name.size() > MaximumString || !local)
                return AssetError {AssetErrorCode::InvalidArgument, "Invalid import fingerprint identity key/ID"};
            if (!assigned.insert(local).second)
                return AssetError {AssetErrorCode::DuplicateID, "Duplicate import fingerprint LocalID"};
        }
        // Sort references instead of copying potentially large settings/paths.
        std::vector<const SourceFingerprintInput*>  sources;
        std::vector<const ProductFingerprintInput*> products;
        sources.reserve(input.Sources.size());
        products.reserve(input.Products.size());
        for (const auto& source : input.Sources)
            sources.push_back(&source);
        for (const auto& product : input.Products)
            products.push_back(&product);
        std::sort(sources.begin(), sources.end(), [](const auto* left, const auto* right) {
            return left->ID.GetUUID() < right->ID.GetUUID();
        });
        std::sort(products.begin(), products.end(), [](const auto* left, const auto* right) {
            if (left->ID.GetSourceID() != right->ID.GetSourceID())
                return left->ID.GetSourceID().GetUUID() < right->ID.GetSourceID().GetUUID();
            return left->ID.GetLocalID() < right->ID.GetLocalID();
        });
        for (size_t i = 1; i < sources.size(); ++i)
            if (sources[i - 1]->ID == sources[i]->ID)
                return AssetError {AssetErrorCode::DuplicateID, "Duplicate source fingerprint dependency"};
        for (size_t i = 1; i < products.size(); ++i)
            if (products[i - 1]->ID == products[i]->ID)
                return AssetError {AssetErrorCode::DuplicateID, "Duplicate Product fingerprint dependency"};
        CanonicalKey key;
        key.String("Kans3D.ImportFingerprint");
        key.Number(2); // Canonical encoding schema, independent of importer version.
        key.Source(input.Source);
        key.String(input.Importer);
        key.Number(input.ImporterVersion);
        key.String(input.Toolchain);
        key.String(input.Target);
        key.Number(input.Settings.size());
        for (const auto& [name, value] : input.Settings)
        {
            key.String(name);
            key.String(value);
        }
        key.Number(input.LocalIDs.size());
        for (const auto& [name, local] : input.LocalIDs)
        {
            key.String(name);
            key.Number(local);
        }
        key.Number(sources.size());
        for (const auto* source : sources)
            key.Source(*source);
        key.Number(products.size());
        for (const auto* product : products)
        {
            key.Number(product->ID.GetSourceID().GetUUID());
            key.Number(product->ID.GetLocalID());
            key.String(product->ContentDigest);
        }
        if (!key.Valid)
            return AssetError {AssetErrorCode::InvalidArgument,
                               "Import fingerprint encoding exceeds byte/string limit"};
        return ComputeContentDigest(key.Bytes);
    }
} // namespace Kans
