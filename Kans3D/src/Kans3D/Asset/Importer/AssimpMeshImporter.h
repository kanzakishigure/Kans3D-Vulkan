#pragma once

#include "Kans3D/Renderer/Resource/Mesh.h"

#include <filesystem>
#include <vector>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Kans
{
    // 将 Assimp 场景中的网格和材质转换为引擎网格源。
    class AssimpMeshImporter
    {
    public:
        explicit AssimpMeshImporter(const std::filesystem::path& path);

        // 解析模型并创建材质纹理与网格缓冲，需要有效的图形上下文。
        Ref<MeshSource> ImportToMeshSource();

        // 解析模型和外部纹理像素，返回尚未上传 GPU 的网格源。
        Ref<MeshSource> ImportToMeshSourceCpu();

    private:
        // 按节点顺序递归收集引用的网格，不应用节点变换。
        void ProcessNode(const aiNode* node, const aiScene* scene, Ref<MeshSource>& ms);

        // 追加顶点、三角形索引和材质，记录子网格在共享数组中的范围。
        SubMesh ProcessMesh(const aiMesh* mesh, const aiScene* scene, Ref<MeshSource>& ms);

        // 根据 Assimp 材质的外部贴图引用创建材质纹理。
        void ImportMaterial(const aiMaterial* aiMat, const aiScene* scene, Ref<MeshSource>& ms);

        // 处理网格偏移与法线，并为各子网格创建顶点数组和缓冲。
        void GenVertexArrays(Ref<MeshSource>& ms);

        // 创建材质并收集外部贴图的像素数据和绑定信息。
        void ImportMaterialCpu(const aiMaterial* aiMat, const aiScene* scene, Ref<MeshSource>& ms);

    private:
        const std::filesystem::path m_Path;
        bool                        m_CpuOnly = false; // 材质贴图是否保留为待上传的像素数据
    };

} // namespace Kans
