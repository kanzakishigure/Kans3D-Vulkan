#pragma once

#include "Kans3D/Asset/Asset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Kans
{
    // 保存资产导入的源路径、输出信息和选项值。
    struct ImportConfig
    {
        std::filesystem::path SourcePath; // 源模型文件路径

        std::filesystem::path OutputDirectory; // 输出目录
        std::string           AssetName;       // 资产名称

        // 后端选择选项
        int  SelectedBackendIndex = 0;    // 0 为自动选择，正数为从 1 开始的后端索引
        bool bAutoSelectBackend   = true; // 是否启用自动选择

        // 导入内容选项
        bool bImportMaterials = true;  // 是否导入材质
        bool bImportTextures  = true;  // 是否导入纹理
        bool bImportLights    = false; // 是否导入灯光
        bool bImportCameras   = false; // 是否导入相机

        // 材质选项
        bool        bCreateMaterialAssets    = true; // 是否创建材质资产
        bool        bSearchExistingMaterials = true; // 是否查找已有材质
        std::string MaterialSearchPath;              // 材质查找路径

        // 变换选项
        bool  bApplyRootTransform = true; // 是否应用根节点变换
        float ImportUniformScale  = 1.0f; // 统一缩放系数

        // 网格处理选项
        bool bGenerateLightmapUVs = false; // 是否生成光照贴图 UV
        bool bCombineMeshes       = false; // 是否合并子网格
        bool bRemoveDegenerates   = true;  // 是否移除退化三角形

        AssetID ExistingAssetID = AssetID {}; // 关联的既有资产标识

        // 检查路径和名称是否非空，以及缩放系数是否为正数。
        bool IsValid() const
        {
            return !SourcePath.empty() && !OutputDirectory.empty() && !AssetName.empty() && ImportUniformScale > 0.0f;
        }

        // 设置源路径和输出目录，以源文件名（不含扩展名）作为资产名称。
        static ImportConfig FromFile(const std::filesystem::path& sourcePath, const std::filesystem::path& outputDir)
        {
            ImportConfig config;
            config.SourcePath      = sourcePath;
            config.OutputDirectory = outputDir;
            config.AssetName       = sourcePath.stem().string();
            return config;
        }
    };

} // namespace Kans
