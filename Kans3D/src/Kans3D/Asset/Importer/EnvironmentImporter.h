#pragma once
#include <filesystem>
#include "Kans3D/Asset/AssetMetadata.h"
#include "LegacyAssetImporterBase.h"
namespace Kans
{
    // 环境资源导入接口声明，成员函数尚未实现。
    class EnvironmentImporter : public LegacyAssetImporterBase
    {
    public:
        void Import(const Ref<AssetMetadata>& metadata, Ref<Asset>& asset) const override;
        bool TryLoadData(const Ref<AssetMetadata>& metadata, Ref<Asset>& asset) const override;

    private:
        void PrepareEnvironment();

    private:
        const std::filesystem::path m_Path;
    };
} // namespace Kans
