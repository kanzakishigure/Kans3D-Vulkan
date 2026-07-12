#pragma once

#include "Kans3D/Renderer/Resource/Mesh.h"

#include <filesystem>
#include <vector>
#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags

namespace Kans
{
	// ============================================================
	// AssimpMeshImporter — 基于 Assimp 的网格导入器
	//
	// 职责：
	//   将任意 Assimp 支持的 3D 模型文件导入为完整的 MeshSource。
	//   MeshSource 本身是纯数据容器，所有导入逻辑由本类负责。
	//
	// 导入流程：
	//   1. 预校验（文件存在 / 大小 / Assimp 解析）
	//   2. 创建空 MeshSource
	//   3. ProcessNode → 递归遍历节点树
	//   4. ProcessMesh → 提取顶点/索引/材质
	//   5. BuildMaterialTable → 组装材质表
	//   6. GenVertexArrays → 创建 GPU VA/VB/IB
	// ============================================================

	class AssimpMeshImporter
	{
	public:
		explicit AssimpMeshImporter(const std::filesystem::path& path);

		// 主入口：从文件导入完整 MeshSource（含 GPU 缓冲，同步模式）
		Ref<MeshSource> ImportToMeshSource();

		// CPU-only 导入：只做 CPU 侧工作（解析 + 图片加载到内存），不创建任何 GL 资源
		// 返回的 MeshSource::IsGpuReady() == false，需要在主线程调用 FinalizeGpuResources()
		Ref<MeshSource> ImportToMeshSourceCpu();

	private:
		// ── 节点递归 ──
		void ProcessNode(const aiNode* node, const aiScene* scene, Ref<MeshSource>& ms);

		// ── 单网格处理 ──
		SubMesh ProcessMesh(const aiMesh* mesh, const aiScene* scene, Ref<MeshSource>& ms);

		// ── 材质导入 ──
		void ImportMaterial(const aiMaterial* aiMat, const aiScene* scene, Ref<MeshSource>& ms);

		// ── GPU 缓冲生成（会调用 OpenGL，必须在主线程执行）──
		void GenVertexArrays(Ref<MeshSource>& ms);

		// ── 材质导入（CPU-only 版本，不创建 GL 纹理）──
		void ImportMaterialCpu(const aiMaterial* aiMat, const aiScene* scene, Ref<MeshSource>& ms);

	private:
		const std::filesystem::path m_Path;
		bool m_CpuOnly = false;  // 标记当前导入是否为 CPU-only 模式
	};

} // namespace Kans
