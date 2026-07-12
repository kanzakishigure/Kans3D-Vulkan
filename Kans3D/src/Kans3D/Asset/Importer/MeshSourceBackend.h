#pragma once

#include "Kans3D/Renderer/Resource/Mesh.h"

#include <filesystem>
#include <string>
#include <functional>
#include <vector>

namespace Kans
{
	// ============================================================
	// MeshSourceBackend — 网格源导入后端抽象接口（V3 增强版）
	//
	// V3 新增：
	//   1. Import 增加 ProgressCallback 参数（异步进度报告）
	//   2. 新增 Preview() 方法：仅读取元数据，不创建 GPU 资源
	//   3. 新增 GetFileInfo()：返回文件基本信息
	//
	// 设计目标：
	//   MeshSourceImporter 不直接依赖任何具体的导入库（Assimp、
	//   FBX SDK、glTF 等），而是通过此接口委托给可插拔的后端。
	// ============================================================

	// ---- 进度回调类型 ----
	// 参数：percentage (0.0~1.0), phase description
	using BackendProgressCallback = std::function<void(float, const char*)>;

	// ---- 文件预览信息（轻量，不创建 GPU 资源）----
	struct MeshSourcePreview
	{
		std::string FormatName;        // 格式名称，如 "FBX 7.5", "glTF 2.0"
		uint32_t   VertexCount   = 0;
		uint32_t   TriangleCount = 0;
		uint32_t   SubMeshCount  = 0;
		uint32_t   MaterialCount = 0;
		uint32_t   LightCount    = 0;
		uint32_t   CameraCount   = 0;
		uint32_t   BoneCount     = 0;
		uint32_t   AnimationCount = 0;
		bool       HasEmbeddedTextures = false;
		float      BoundingRadius = 0.0f;  // 包围球半径

		bool IsValid() const { return VertexCount > 0 && TriangleCount > 0; }
	};

	class MeshSourceBackend
	{
	public:
		MeshSourceBackend() = default;
		virtual ~MeshSourceBackend() noexcept = default;

		// ---- 后端标识 ----
		virtual const char* GetName() const = 0;

		// ---- 能力查询 ----
		virtual bool Supports(const std::filesystem::path& extension) const = 0;

		// ---- 完整导入 ----
		// V3: 新增 progress 回调，支持异步进度报告
		virtual Ref<MeshSource> Import(
			const std::filesystem::path& filePath,
			BackendProgressCallback progress = nullptr) = 0;

		// ---- 轻量预览（V3 新增）----
		// 仅解析文件头和基本统计信息，不创建 GPU 资源。
		// 用于导入前展示网格预览信息。
		// 返回空的 MeshSourcePreview 表示无法预览（格式不支持等）。
		virtual MeshSourcePreview Preview(const std::filesystem::path& filePath)
		{
			// 默认实现：返回空预览（子类应重写）
			return MeshSourcePreview{};
		}

		// ---- 支持预览的能力查询 ----
		virtual bool SupportsPreview() const { return false; }

		// ---- 轻量预检 ----
		virtual bool TryLoad(const std::filesystem::path& filePath) const
		{
			if (!Supports(filePath.extension()))
				return false;
			if (!std::filesystem::exists(filePath))
				return false;
			return std::filesystem::file_size(filePath) > 0;
		}
	};

} // namespace Kans
