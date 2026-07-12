#pragma once

#include "MeshSourceBackend.h"
#include "Kans3D/Asset/Importer/AssetImporterbase.h"
#include "Kans3D/Asset/AssetMetaData.h"

#include <vector>

namespace Kans
{
	// ============================================================
	// MeshSourceImporter — 网格源导入器（V3）
	//
	// V3 新增：
	//   - ImportMeshSource() 增加 progress 回调参数
	//   - ImportMeshSourceAsync() 接受 ImportConfig
	//   - PreviewMeshSource() 调用后端预览
	//   - GetBackendNames() 列出可用后端
	// ============================================================

	class MeshSourceImporter : public AssetImporterbase
	{
	public:
		MeshSourceImporter() = default;

		// ---- AssetImporterbase 接口 ----
		void Importe(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset) const override;
		bool TryLoadData(const Ref<AssetMetaData>& metadata, Ref<Asset>& asset) const override;

		// ---- 后端管理 ----
		static void RegisterBackend(Scope<MeshSourceBackend> backend);
		static bool UnregisterBackend(const char* name);
		static size_t GetBackendCount() { return s_Backends.size(); }

		// 获取所有已注册后端名称（用于 UI 下拉选择）
		static std::vector<const char*> GetBackendNames();

		// ---- 导入 ----

		// 同步导入（带进度回调）
		static Ref<MeshSource> ImportMeshSource(
			const std::filesystem::path& filePath,
			BackendProgressCallback progress = nullptr,
			int preferredBackendIndex = -1);

		// 快速预检
		static bool CanImport(const std::filesystem::path& filePath);

		// ---- 预览（V3 新增）----
		// 在导入前获取文件统计信息，不创建 GPU 资源
		static MeshSourcePreview PreviewMeshSource(const std::filesystem::path& filePath);

		static const char* GetLastUsedBackendName() { return s_LastUsedBackend; }

	private:
		static std::vector<Scope<MeshSourceBackend>> s_Backends;
		static thread_local const char* s_LastUsedBackend;
	};

} // namespace Kans
