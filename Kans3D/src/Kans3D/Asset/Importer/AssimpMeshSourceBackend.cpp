#include "kspch.h"
#include "AssimpMeshSourceBackend.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Kans
{
    static constexpr uint32_t kAssimpImportFlags = aiProcess_CalcTangentSpace | aiProcess_Triangulate |
                                                   aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
                                                   aiProcess_GenSmoothNormals;

    // 预览将面三角化并按图元类型拆分网格，用于统计场景数据。
    static constexpr uint32_t kAssimpPreviewFlags = aiProcess_Triangulate | aiProcess_SortByPType;

    const std::unordered_set<std::string>& AssimpMeshSourceBackend::GetSupportedExtensions()
    {
        static const std::unordered_set<std::string> s_Extensions = {
            ".obj", ".fbx", ".gltf", ".glb", ".dae", ".ply",     ".stl",     ".3ds",  ".blend", ".x3d",
            ".lwo", ".lws", ".ase",  ".md2", ".md3", ".md5mesh", ".md5anim", ".ms3d", ".smd",   ".irrmesh",
            ".irr", ".nff", ".off",  ".raw", ".ter", ".cob",     ".q3bsp",   ".q3o",  ".csm",   ".bvh",
            ".b3d", ".ndo", ".ifc",  ".xgl", ".zgl", ".ac",      ".dxf"};
        return s_Extensions;
    }

    bool AssimpMeshSourceBackend::Supports(const std::filesystem::path& extension) const
    {
        return GetSupportedExtensions().count(extension.string()) > 0;
    }

    Ref<MeshSource> AssimpMeshSourceBackend::Import(const std::filesystem::path& filePath,
                                                    BackendProgressCallback      progress)
    {
        CORE_INFO_TAG("AssimpBackend", "Importing: {}", filePath.string());

        if (progress)
            progress(0.0f, "Initializing...");

        AssimpMeshImporter assimpImporter(filePath);

        if (progress)
            progress(0.3f, "Calling Assimp ReadFile...");

        Ref<MeshSource> meshSource = assimpImporter.ImportToMeshSource();

        if (progress)
            progress(0.8f, "Processing result...");

        if (!meshSource)
        {
            CORE_ERROR_TAG("AssimpBackend", "Import failed for: {}", filePath.string());
            if (progress)
                progress(1.0f, "Import failed");
            return nullptr;
        }

        if (progress)
            progress(1.0f, "Import complete");

        CORE_INFO_TAG("AssimpBackend",
                      "Imported: {} ({} submeshes)",
                      filePath.filename().string(),
                      meshSource->GetSubMesh().size());

        return meshSource;
    }

    MeshSourcePreview AssimpMeshSourceBackend::Preview(const std::filesystem::path& filePath)
    {
        MeshSourcePreview preview;

        if (!std::filesystem::exists(filePath))
            return preview;

        try
        {
            Assimp::Importer importer;
            const aiScene*   scene = importer.ReadFile(filePath.string(), kAssimpPreviewFlags);

            if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
                return preview;

            // 按场景的网格数组累计顶点、面和子网格数量。
            for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
            {
                const aiMesh* mesh = scene->mMeshes[i];
                if (!mesh)
                    continue;

                preview.VertexCount += mesh->mNumVertices;
                preview.TriangleCount += mesh->mNumFaces;
                ++preview.SubMeshCount;
            }

            preview.MaterialCount = scene->mNumMaterials;
            preview.LightCount    = scene->mNumLights;
            preview.CameraCount   = scene->mNumCameras;

            if (scene->HasAnimations())
                preview.AnimationCount = scene->mNumAnimations;

            // 当前纹理标记依据材质是否引用漫反射贴图设置。
            for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
            {
                aiMaterial* mat = scene->mMaterials[i];
                if (mat && mat->GetTextureCount(aiTextureType_DIFFUSE) > 0)
                {
                    preview.HasEmbeddedTextures = true;
                    break;
                }
            }

            // 使用去掉点号的大写扩展名作为格式名称。
            std::string ext = filePath.extension().string();
            if (!ext.empty())
                preview.FormatName = ext.substr(1);
            std::transform(preview.FormatName.begin(), preview.FormatName.end(), preview.FormatName.begin(), ::toupper);

            CORE_INFO_TAG("AssimpBackend",
                          "Preview: {} → {} verts, {} tris, {} subs, {} mats",
                          filePath.filename().string(),
                          preview.VertexCount,
                          preview.TriangleCount,
                          preview.SubMeshCount,
                          preview.MaterialCount);
        }
        catch (const std::exception& e)
        {
            CORE_ERROR_TAG("AssimpBackend", "Preview failed for {}: {}", filePath.string(), e.what());
        }

        return preview;
    }

    bool AssimpMeshSourceBackend::TryLoad(const std::filesystem::path& filePath) const
    {
        if (!Supports(filePath.extension()))
            return false;
        if (!std::filesystem::exists(filePath))
            return false;
        if (std::filesystem::file_size(filePath) < 64)
            return false;

        Assimp::Importer quickImporter;
        const aiScene*   scene = quickImporter.ReadFile(filePath.string(), aiProcess_Triangulate);
        return scene && !(scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) && scene->mRootNode && scene->mNumMeshes > 0;
    }

} // namespace Kans
