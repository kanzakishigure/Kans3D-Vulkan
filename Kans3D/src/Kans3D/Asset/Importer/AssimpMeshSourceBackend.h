#pragma once

#include "MeshSourceBackend.h"
#include "Kans3D/Asset/Importer/AssimpMeshImporter.h"

namespace Kans
{
	// ============================================================
	// AssimpMeshSourceBackend — 基于 Assimp 库的网格源导入后端（V3 增强版）
	//
	// V3 新增：
	//   - 实现 Preview()：使用轻量 Assimp 读取获取统计信息
	//   - Import() 添加 progress 回调支持
	//   - SupportsPreview() 返回 true
	// ============================================================

	class AssimpMeshSourceBackend : public MeshSourceBackend
	{
	public:
		AssimpMeshSourceBackend() = default;

		const char* GetName() const override { return "Assimp 5.4 (Open Asset Import Library)"; }

		bool Supports(const std::filesystem::path& extension) const override;

		Ref<MeshSource> Import(
			const std::filesystem::path& filePath,
			BackendProgressCallback progress = nullptr) override;

		MeshSourcePreview Preview(const std::filesystem::path& filePath) override;
		bool SupportsPreview() const override { return true; }

		bool TryLoad(const std::filesystem::path& filePath) const override;

	private:
		static const std::unordered_set<std::string>& GetSupportedExtensions();
	};

} // namespace Kans
