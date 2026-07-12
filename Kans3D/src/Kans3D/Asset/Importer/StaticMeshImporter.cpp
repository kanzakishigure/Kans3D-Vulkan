#include "kspch.h"
#include "Kans3D/Asset/Importer/StaticMeshImporter.h"

#include "Kans3D/Asset/Importer/MeshSourceImporter.h"
#include "Kans3D/Renderer/Resource/Mesh.h"

#include <fstream>

namespace Kans
{
	// ================================================================
	// .ksmesh 序列化格式定义
	// ================================================================
	static constexpr uint32_t KSMESH_MAGIC   = 0x4D534B4B;  // "KKSM"
	static constexpr uint32_t KSMESH_VERSION = 1;

	#pragma pack(push, 1)
	struct KSMeshHeader
	{
		uint32_t Magic          = KSMESH_MAGIC;
		uint32_t Version        = KSMESH_VERSION;
		UUID     SourceHandle;
		uint32_t SubMeshCount;
		uint32_t MaterialCount;
		uint32_t Reserved[4] = {0};
	};
	#pragma pack(pop)

	// ============================================================
	// Importe — 主入口
	// ============================================================

	void StaticMeshImporter::Importe(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset) const
	{
		if (!metadata)
		{
			CORE_ERROR("StaticMeshImporter::Importe — metadata is null");
			asset = nullptr;
			return;
		}

		const auto& filePath = metadata->FilePath;
		if (filePath.empty() || !std::filesystem::exists(filePath))
		{
			CORE_ERROR("StaticMeshImporter::Importe — file not found: {}", filePath.string());
			asset = nullptr;
			return;
		}

		// ============================================================
		// MODE_SERIALIZED：.ksmesh 文件 → 反序列化
		// ============================================================
		if (IsSerializedFormat(filePath))
		{
			StaticMeshSourceRef sourceRef;
			if (!LoadKSMeshHeader(filePath, sourceRef))
			{
				CORE_ERROR("StaticMeshImporter::Importe — failed to read .ksmesh header: {}", filePath.string());
				asset = nullptr;
				return;
			}

			Ref<StaticMesh> staticMesh = CreateRef<StaticMesh>(nullptr);
			staticMesh->Handle = metadata->Handle;

			CORE_INFO("StaticMeshImporter — deserialized .ksmesh: {} (source handle: {})",
			          filePath.filename().string(), (uint64_t)sourceRef.SourceHandle);

			asset = staticMesh;
			return;
		}

		// ============================================================
		// MODE_SOURCE：原始模型文件 → MeshSourceImporter → MeshSource → StaticMesh
		//
		// ★ 不再直接调用 AssimpMeshImporter，而是通过 MeshSourceImporter 的
		//   静态方法 ImportMeshSource() 获取 MeshSource。
		//   MeshSourceImporter 内部根据已注册后端自动选择导入方式。
		// ============================================================
		if (!IsSupportedSourceExtension(filePath.extension()))
		{
			CORE_ERROR("StaticMeshImporter::Importe — unsupported format: {}", filePath.extension().string());
			asset = nullptr;
			return;
		}

		// ◆ 步骤 1：通过 MeshSourceImporter 导入原始数据
		Ref<MeshSource> meshSource = MeshSourceImporter::ImportMeshSource(filePath);
		if (!meshSource)
		{
			CORE_ERROR("StaticMeshImporter::Importe — MeshSourceImporter failed for: {}", filePath.string());
			asset = nullptr;
			return;
		}

		CORE_INFO("StaticMeshImporter — MeshSource imported via '{}': {} submeshes",
		          MeshSourceImporter::GetLastUsedBackendName(),
		          meshSource->GetSubMesh().size());

		// ◆ 步骤 2：引擎从 MeshSource 生成 StaticMesh
		Ref<StaticMesh> staticMesh = GenerateStaticMeshFromSource(meshSource);
		if (!staticMesh)
		{
			CORE_ERROR("StaticMeshImporter::Importe — failed to generate StaticMesh from MeshSource");
			asset = nullptr;
			return;
		}

		staticMesh->Handle = metadata->Handle;

		CORE_INFO("StaticMeshImporter — generated StaticMesh from MeshSource: {} ({} submeshes)",
		          filePath.filename().string(),
		          meshSource->GetSubMesh().size());

		asset = staticMesh;
	}

	// ============================================================
	// GenerateStaticMeshFromSource — 引擎生成 StaticMesh
	// ============================================================

	Ref<StaticMesh> StaticMeshImporter::GenerateStaticMeshFromSource(
		const Ref<MeshSource>& meshSource,
		const std::vector<uint32_t>& subMeshIndices)
	{
		if (!meshSource)
		{
			CORE_ERROR("StaticMeshImporter::GenerateStaticMeshFromSource — MeshSource is null");
			return nullptr;
		}

		const auto& subMeshes = meshSource->GetSubMesh();
		if (subMeshes.empty())
		{
			CORE_ERROR("StaticMeshImporter::GenerateStaticMeshFromSource — MeshSource has no submeshes");
			return nullptr;
		}

		Ref<StaticMesh> staticMesh = CreateRef<StaticMesh>(meshSource);

		if (subMeshIndices.empty())
		{
			std::vector<uint32_t> allIndices(subMeshes.size());
			for (size_t i = 0; i < subMeshes.size(); ++i)
				allIndices[i] = static_cast<uint32_t>(i);
			staticMesh->SetSubMesh(allIndices);
		}
		else
		{
			staticMesh->SetSubMesh(subMeshIndices);
		}

		if (meshSource->GetMaterialTable())
		{
			// MaterialTable 已通过 MeshSource 构造时传入
		}

		CORE_INFO("StaticMeshImporter::GenerateStaticMeshFromSource — generated with {} submeshes",
		          staticMesh->GetSubMesh().size());

		return staticMesh;
	}

	// ============================================================
	// MODE_SERIALIZED 支持
	// ============================================================

	bool StaticMeshImporter::IsSerializedFormat(const std::filesystem::path& filePath)
	{
		return filePath.extension() == ".ksmesh";
	}

	bool StaticMeshImporter::LoadKSMeshHeader(const std::filesystem::path& filePath,
	                                           StaticMeshSourceRef& outSourceRef)
	{
		std::ifstream file(filePath, std::ios::binary);
		if (!file.is_open())
			return false;

		KSMeshHeader header{};
		file.read(reinterpret_cast<char*>(&header), sizeof(KSMeshHeader));

		if (!file || header.Magic != KSMESH_MAGIC)
		{
			CORE_ERROR("StaticMeshImporter::LoadKSMeshHeader — invalid magic for: {}", filePath.string());
			return false;
		}

		if (header.Version != KSMESH_VERSION)
		{
			CORE_WARN("StaticMeshImporter::LoadKSMeshHeader — version mismatch (got {}, expected {})",
			          header.Version, KSMESH_VERSION);
		}

		outSourceRef.SourceHandle = header.SourceHandle;
		outSourceRef.SourcePath   = filePath;
		return true;
	}

	// ============================================================
	// TryLoadData — 预检
	//
	// ★ MODE_SOURCE 预检委托 MeshSourceImporter，不再直接调 Assimp
	// ============================================================

	bool StaticMeshImporter::TryLoadData(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset) const
	{
		if (!metadata)
			return false;

		const auto& filePath = metadata->FilePath;
		if (filePath.empty() || !std::filesystem::exists(filePath))
			return false;

		if (IsSerializedFormat(filePath))
		{
			StaticMeshSourceRef dummy;
			asset = nullptr;
			return LoadKSMeshHeader(filePath, dummy);
		}

		// 委托 MeshSourceImporter 进行后端预检
		asset = nullptr;
		return MeshSourceImporter::CanImport(filePath);
	}

	// ============================================================
	// IsSupportedSourceExtension — 委托 MeshSourceImporter
	// ============================================================

	bool StaticMeshImporter::IsSupportedSourceExtension(const std::filesystem::path& extension)
	{
		// 通过构造临时路径委托后端注册表判断
		return MeshSourceImporter::CanImport(std::filesystem::path("_dummy").replace_extension(extension));
	}

} // namespace Kans
