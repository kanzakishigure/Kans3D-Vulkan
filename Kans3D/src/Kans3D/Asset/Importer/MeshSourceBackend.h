#pragma once

#include "Kans3D/Renderer/Resource/Mesh.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Kans
{
    // 导入进度回调，参数为完成比例（0～1）和阶段描述。
    using BackendProgressCallback = std::function<void(float, const char*)>;

    // 后端提供的模型统计信息，未填充的字段保留默认值。
    struct MeshSourcePreview
    {
        std::string FormatName; // 后端提供的格式名称
        uint32_t    VertexCount         = 0;
        uint32_t    TriangleCount       = 0;
        uint32_t    SubMeshCount        = 0;
        uint32_t    MaterialCount       = 0;
        uint32_t    LightCount          = 0;
        uint32_t    CameraCount         = 0;
        uint32_t    BoneCount           = 0;
        uint32_t    AnimationCount      = 0;
        bool        HasEmbeddedTextures = false;
        float       BoundingRadius      = 0.0f; // 包围球半径

        // 同时包含顶点和三角形时，预览结果有效。
        bool IsValid() const { return VertexCount > 0 && TriangleCount > 0; }
    };

    // 定义网格源的格式查询、导入、预览和文件预检接口。
    class MeshSourceBackend
    {
    public:
        MeshSourceBackend()                   = default;
        virtual ~MeshSourceBackend() noexcept = default;

        // 返回后端名称。
        virtual const char* GetName() const = 0;

        // 查询后端是否支持指定扩展名。
        virtual bool Supports(const std::filesystem::path& extension) const = 0;

        // 导入网格源，可通过回调报告进度；失败时返回空引用。
        virtual Ref<MeshSource> Import(const std::filesystem::path& filePath,
                                       BackendProgressCallback      progress = nullptr) = 0;

        // 获取模型统计信息，默认返回空预览。
        virtual MeshSourcePreview Preview(const std::filesystem::path& filePath) { return MeshSourcePreview {}; }

        // 查询后端是否提供预览功能，默认不支持。
        virtual bool SupportsPreview() const { return false; }

        // 默认仅检查扩展名、文件是否存在及大小是否非零。
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
