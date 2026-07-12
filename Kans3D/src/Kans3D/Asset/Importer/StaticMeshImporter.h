#pragma once

#include "Kans3D/Asset/Importer/AssetImporterbase.h"
#include "Kans3D/Asset/AssetMetaData.h"
#include "Kans3D/Core/UUID.h"
#include "Kans3D/Renderer/Resource/Mesh.h"

#include <filesystem>
#include <vector>

namespace Kans
{
	// ============================================================
	// StaticMeshImporter — 引擎静态网格加载器
	//
	// 职责：
	//   不是从原始模型文件直接生成 StaticMesh！
	//   RawFile(.fbx) → MeshSourceImporter → MeshSource
	//                                ↓
	//   StaticMeshImporter ← 引擎生成 ←┘  (或从 .ksmesh 反序列化)
	//
	// 加载模式：
	//   MODE_SOURCE:  metadata.FilePath → 原始模型文件
	//                 ★ 委托 MeshSourceImporter 导入 MeshSource（不直接调 Assimp）
	//                 然后调用 GenerateStaticMeshFromSource() 引擎生成
	//
	//   MODE_SERIALIZED: metadata.FilePath → .ksmesh 序列化文件
	//                     读取头信息，创建轻量 StaticMesh（MeshSource 延迟绑定）
	//
	// 修正点：
	//   1. 移除 ImportMeshSourceFromFile() —— 不再硬编码 Assimp
	//   2. MODE_SOURCE 通过 MeshSourceImporter 获取 MeshSource
	//   3. IsSupportedSourceExtension → 委托 MeshSourceImporter::CanImport()
	//   4. TryLoadData 不再直接调 Assimp::Importer
	// ============================================================

	// ---- .ksmesh 序列化文件中的 MeshSource 引用 ----
	struct StaticMeshSourceRef
	{
		AssetHandle SourceHandle = 0;
		std::filesystem::path SourcePath;
	};

	class StaticMeshImporter : public AssetImporterbase
	{
	public:
		StaticMeshImporter() = default;

		void Importe(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset) const override;
		bool TryLoadData(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset) const override;

		// 引擎生成：从 MeshSource 生成 StaticMesh
		static Ref<StaticMesh> GenerateStaticMeshFromSource(
			const Ref<MeshSource>& meshSource,
			const std::vector<uint32_t>& subMeshIndices = {});

	private:
		// MODE_SERIALIZED
		static bool IsSerializedFormat(const std::filesystem::path& filePath);
		static bool LoadKSMeshHeader(const std::filesystem::path& filePath,
		                             StaticMeshSourceRef& outSourceRef);

		// 委托 MeshSourceImporter 判断
		static bool IsSupportedSourceExtension(const std::filesystem::path& extension);
	};

} // namespace Kans
