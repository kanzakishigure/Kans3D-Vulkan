#include "kspch.h"
#include "AssetProvider.h"

namespace Kans
{
    std::variant<Ref<LooseFileAssetProvider>, AssetError>
    LooseFileAssetProvider::Open(const std::filesystem::path& root)
    {
        if (!root.is_absolute())
            return AssetError {AssetErrorCode::InvalidArgument, "Asset provider root must be absolute"};
        try
        {
            if (!std::filesystem::is_directory(std::filesystem::symlink_status(root)))
                return AssetError {AssetErrorCode::InvalidArgument, "Asset provider root must be a real directory"};
            return Ref<LooseFileAssetProvider>(new LooseFileAssetProvider(std::filesystem::canonical(root)));
        }
        catch (const std::filesystem::filesystem_error& error)
        {
            return AssetError {AssetErrorCode::IoError, error.what()};
        }
    }

    std::variant<std::vector<std::byte>, AssetError> LooseFileAssetProvider::Read(const ProductRecord& record) const
    {
        return ReadRegisteredProduct(m_Root, record);
    }
} // namespace Kans
