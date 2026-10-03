#pragma once

#include "Kans3D/Asset/Importer/AssimpMeshImporter.h"
#include "MeshSourceBackend.h"

namespace Kans
{
    // 提供基于 Assimp 的格式支持查询、文件预检、场景统计和网格源导入。
    class AssimpMeshSourceBackend : public MeshSourceBackend
    {
    public:
        AssimpMeshSourceBackend() = default;

        const char* GetName() const override { return "Assimp 5.4 (Open Asset Import Library)"; }

        // 查询后端声明的扩展名集合，参数应包含点号并使用小写。
        bool Supports(const std::filesystem::path& extension) const override;

        // 同步导入网格源，进度回调在调用线程中报告导入阶段。
        Ref<MeshSource> Import(const std::filesystem::path& filePath,
                               BackendProgressCallback      progress = nullptr) override;

        // 解析场景并统计网格、材质、灯光、相机和动画数量。
        MeshSourcePreview Preview(const std::filesystem::path& filePath) override;
        bool              SupportsPreview() const override { return true; }

        // 检查扩展名与文件大小，并验证 Assimp 能否解析出含网格的完整场景。
        bool TryLoad(const std::filesystem::path& filePath) const override;

    private:
        static const std::unordered_set<std::string>& GetSupportedExtensions();
    };

} // namespace Kans
