#pragma once

#include "Kans3D/Asset/Asset.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Kans
{
	// ============================================================
	// ImportConfig — 资产导入配置
	//
	// 存储用户在导入面板中设置的全部选项。在异步导入任务
	// 启动前由 ImporterPanel 填充，传递给 ImportTask。
	//
	// 设计参考 UE5 的 FBX Import Options 对话框。
	// ============================================================

	struct ImportConfig
	{
		// ---- 源文件 ----
		std::filesystem::path SourcePath;         // 原始模型文件路径 (.fbx, .gltf, ...)

		// ---- 输出设置 ----
		std::filesystem::path OutputDirectory;    // MeshSource 输出目录
		std::string           AssetName;          // 资产名称（默认 = 文件名去掉扩展名）

		// ---- 后端选择 ----
		int    SelectedBackendIndex = 0;          // -1 = 自动选择, >=0 = 使用指定后端
		bool   bAutoSelectBackend  = true;        // 自动选择第一个支持该格式的后端

		// ---- 网格选项 ----
		bool   bImportMaterials   = true;         // 导入材质信息
		bool   bImportTextures    = true;         // 导入纹理（嵌入/外部）
		bool   bImportLights      = false;        // 导入场景灯光
		bool   bImportCameras     = false;        // 导入场景相机

		// ---- 材质设置 ----
		bool   bCreateMaterialAssets = true;      // 为每个材质创建 Material 资产
		bool   bSearchExistingMaterials = true;   // 搜索已存在的同名材质复用
		std::string MaterialSearchPath;           // 材质搜索路径（空 = 自动）

		// ---- 变换设置 ----
		bool   bApplyRootTransform = true;        // 应用根节点变换
		float  ImportUniformScale  = 1.0f;        // 统一缩放

		// ---- 网格设置 ----
		bool   bGenerateLightmapUVs = false;      // 生成光照贴图 UV
		bool   bCombineMeshes       = false;      // 合并所有子网格为单个
		bool   bRemoveDegenerates   = true;       // 移除退化三角形

		// ---- 资产句柄 ----
		AssetHandle ExistingAssetHandle = AssetHandle{};       // 如果替换现有资产,指定其句柄

		// ---- 验证 ----
		bool IsValid() const
		{
			return !SourcePath.empty()
				&& !OutputDirectory.empty()
				&& !AssetName.empty()
				&& ImportUniformScale > 0.0f;
		}

		// ---- 工厂方法 ----
		static ImportConfig FromFile(const std::filesystem::path& sourcePath,
		                              const std::filesystem::path& outputDir)
		{
			ImportConfig config;
			config.SourcePath      = sourcePath;
			config.OutputDirectory = outputDir;
			config.AssetName       = sourcePath.stem().string();
			return config;
		}
	};

} // namespace Kans
