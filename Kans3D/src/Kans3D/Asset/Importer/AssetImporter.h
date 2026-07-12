#pragma once

#include "Kans3D/Asset/AssetMetaData.h"
#include "AssetImporterbase.h"

#include <unordered_map>

namespace Kans
{
	// ============================================================
	// AssetImporter — 导入器注册中心
	//
	// 资产管线层次：
	//   RawFile → MeshSourceImporter  → MeshSource  (通过可插拔后端)
	//                  ↓
	//   MeshSource → StaticMeshImporter → StaticMesh (引擎生成)
	//
	//   RawFile → TextureImporter    → Texture2D
	//   RawFile → EnvMapImporter     → EnvironmentMap
	// ============================================================

	class AssetImporter
	{
	public:
		// 注册所有内置导入器（应用启动时调用一次）
		static void Init();

		// 动态注册/注销自定义导入器
		static void RegisterImporter(AssetType type, Scope<AssetImporterbase> importer);
		static void UnregisterImporter(AssetType type);
		static bool HasImporter(AssetType type);

		// 完整导入（委托给对应类型的导入器）
		static void Import(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset);

		// 轻量预检（委托给对应类型的导入器）
		static bool TryLoad(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset);

		// 获取已注册的导入器数量
		static size_t GetImporterCount() { return s_Importers.size(); }

	private:
		static std::unordered_map<AssetType, Scope<AssetImporterbase>> s_Importers;
	};

} // namespace Kans