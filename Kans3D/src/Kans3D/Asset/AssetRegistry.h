#pragma once
#include <map>
#include <optional>
#include <unordered_map>
#include <variant>
#include "Kans3D/Asset/AssetError.h"
#include "Kans3D/Asset/Product/ProductRecord.h"

namespace Kans
{
    // Value snapshot. Coordinator mutations, const snapshots for readers.
    class AssetRegistry
    {
    public:
        std::optional<ProductRecord> Find(AssetID id, const std::string& target = "default") const;
        Ref<AssetMetadata>           GetAssetMetadata(AssetID id) const;
        std::vector<ProductRecord>   GetAllProducts() const;
        [[nodiscard]] std::variant<std::monostate, AssetError>
        ReplaceSourceProducts(SourceAssetID                     source,
                              const std::string&                target,
                              const std::vector<ProductRecord>& products);
        [[nodiscard]] static std::variant<AssetRegistry, AssetError>
             FromProducts(const std::vector<ProductRecord>& products);
        void Swap(AssetRegistry& other) noexcept { m_Products.swap(other.m_Products); }

    private:
        std::unordered_map<AssetID, std::map<std::string, ProductRecord>> m_Products;
    };
    [[nodiscard]] std::variant<std::string, AssetError>   EncodeAssetRegistry(const AssetRegistry& registry);
    [[nodiscard]] std::variant<AssetRegistry, AssetError> DecodeAssetRegistry(const std::string& bytes);
    struct ProductChangeSet
    {
        std::vector<AssetID> Added;
        std::vector<AssetID> Modified;
        std::vector<AssetID> Removed;
    };
    bool SameProductContent(const ProductRecord& a, const ProductRecord& b);
    // Typed byte/reference changes, independent of storage locations/generations.
    [[nodiscard]] std::variant<ProductChangeSet, AssetError>
    CompareAssetRegistries(const AssetRegistry& previous,
                           const AssetRegistry& current,
                           const std::string&   target = "default");
} // namespace Kans
