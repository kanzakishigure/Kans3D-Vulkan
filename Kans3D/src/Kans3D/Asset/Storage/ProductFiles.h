#pragma once
#include <variant>
#include "Kans3D/Asset/AssetError.h"
#include "Kans3D/Asset/Product/ProductRecord.h"

namespace Kans
{
    class ProductStoreLock
    {
    public:
        ~ProductStoreLock();
        ProductStoreLock(const ProductStoreLock&)                                                   = delete;
        ProductStoreLock&                                        operator=(const ProductStoreLock&) = delete;
        static std::variant<Scope<ProductStoreLock>, AssetError> Acquire(const std::filesystem::path& file);

    private:
        explicit ProductStoreLock(intptr_t handle) : m_Handle(handle) {}
        intptr_t m_Handle = -1;
    };

    [[nodiscard]] std::variant<std::monostate, AssetError> WriteProductFile(const std::filesystem::path&  path,
                                                                            const std::vector<std::byte>& bytes);
    [[nodiscard]] std::variant<std::monostate, AssetError>
    ReplaceProductCatalog(const std::filesystem::path& temporary, const std::filesystem::path& destination);
    [[nodiscard]] std::variant<std::monostate, AssetError> SyncProductDirectory(const std::filesystem::path& directory);
    [[nodiscard]] std::variant<std::filesystem::path, AssetError> ResolveProductFile(const std::filesystem::path& root,
                                                                                     const AssetPath& relative);
    [[nodiscard]] std::variant<std::vector<std::byte>, AssetError>
    ReadProductFile(const std::filesystem::path& path, uint64_t maximumBytes = 512 * 1024 * 1024);
    [[nodiscard]] std::variant<std::vector<std::byte>, AssetError>
    ReadRegisteredProduct(const std::filesystem::path& root, const ProductRecord& record);
} // namespace Kans
