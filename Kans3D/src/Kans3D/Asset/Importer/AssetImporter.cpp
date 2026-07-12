#include "kspch.h"
#include "AssetImporter.h"

#include "TextureImporter.h"
#include "StaticMeshImporter.h"
#include "MeshSourceImporter.h"
#include "AssimpMeshSourceBackend.h"

namespace Kans
{
	std::unordered_map<AssetType, Scope<AssetImporterbase>> AssetImporter::s_Importers;

	// ============================================================
	// Init — 注册全部内置导入器 + 后端
	// ============================================================

	void AssetImporter::Init()
	{
		s_Importers.clear();

		// ═══ 步骤 0：初始化 MeshSource 导入后端 ═══
		MeshSourceImporter::RegisterBackend(CreateScope<AssimpMeshSourceBackend>());

		// TODO: 按需注册其他后端
		// MeshSourceImporter::RegisterBackend(CreateScope<FBXSDKBackend>());
		// MeshSourceImporter::RegisterBackend(CreateScope<NativeGLTFBackend>());

		// ═══ 步骤 1：注册纹理导入器 ═══
		s_Importers[AssetType::Texture] = CreateScope<TextureImporter>();

		// ═══ 步骤 2：注册网格管线导入器（两层分离）═══
		// Layer 1: 原始数据 → MeshSource（通过可插拔后端）
		s_Importers[AssetType::MeshSource] = CreateScope<MeshSourceImporter>();
		// Layer 2: MeshSource → StaticMesh（引擎生成 / .ksmesh 反序列化）
		s_Importers[AssetType::StaticMesh] = CreateScope<StaticMeshImporter>();
		s_Importers[AssetType::Mesh]       = CreateScope<StaticMeshImporter>();

		CORE_INFO("AssetImporter — registered {} asset importers, {} mesh backends",
		          s_Importers.size(),
		          MeshSourceImporter::GetBackendCount());
	}

	// ============================================================
	// RegisterImporter / UnregisterImporter / HasImporter
	// ============================================================

	void AssetImporter::RegisterImporter(AssetType type, Scope<AssetImporterbase> importer)
	{
		if (!importer)
		{
			CORE_ERROR("AssetImporter::RegisterImporter — importer is null for type {}", (uint16_t)type);
			return;
		}
		s_Importers[type] = std::move(importer);
	}

	void AssetImporter::UnregisterImporter(AssetType type)
	{
		s_Importers.erase(type);
	}

	bool AssetImporter::HasImporter(AssetType type)
	{
		return s_Importers.find(type) != s_Importers.end();
	}

	// ============================================================
	// Import — 调度导入
	// ============================================================

	void AssetImporter::Import(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset)
	{
		if (!metadata)
		{
			CORE_ERROR("AssetImporter::Import — metadata is null");
			asset = nullptr;
			return;
		}

		auto it = s_Importers.find(metadata->Type);
		if (it == s_Importers.end())
		{
			CORE_ERROR("AssetImporter::Import — no importer registered for AssetType::{} (file: {})",
			           (uint16_t)metadata->Type, metadata->FilePath.string());
			asset = nullptr;
			return;
		}

		it->second->Importe(metadata, asset);
	}

	// ============================================================
	// TryLoad — 预检调度
	// ============================================================

	bool AssetImporter::TryLoad(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset)
	{
		if (!metadata)
			return false;

		auto it = s_Importers.find(metadata->Type);
		if (it == s_Importers.end())
			return false;

		return it->second->TryLoadData(metadata, asset);
	}

} // namespace Kans



