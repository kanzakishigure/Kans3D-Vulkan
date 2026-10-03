#pragma once

#include "MeshSourceBackend.h"

#include "Kans3D/Asset/AssetMetadata.h"

#include <vector>

namespace Kans
{
    // 管理网格源后端，并将导入、预检和预览请求分派给可用后端。
    class MeshSourceImporter
    {
    public:
        MeshSourceImporter() = default;

        // 接管后端所有权，新注册的后端优先被查询。
        static void RegisterBackend(Scope<MeshSourceBackend> backend);
        // 移除首个名称匹配的后端，未找到时返回 false。
        static bool   UnregisterBackend(const char* name);
        static size_t GetBackendCount() { return s_Backends.size(); }

        // 返回选择项名称：索引 0 为自动选择，后端索引从 1 开始。
        static std::vector<const char*> GetBackendNames();

        // 优先尝试指定后端，未成功时按注册列表顺序尝试支持该格式的后端。
        // preferredBackendIndex 大于 0 时指定后端，其余值表示自动选择。
        static Ref<MeshSource> ImportMeshSource(const std::filesystem::path& filePath,
                                                BackendProgressCallback      progress              = nullptr,
                                                int                          preferredBackendIndex = -1);

        // 检查是否有支持该扩展名且通过文件预检的后端。
        static bool CanImport(const std::filesystem::path& filePath);

        // 依次查询支持预览的后端，返回首个有效结果；均失败时返回空预览。
        static MeshSourcePreview PreviewMeshSource(const std::filesystem::path& filePath);

        // 返回当前线程最近一次成功导入所用的后端名称。
        static const char* GetLastUsedBackendName() { return s_LastUsedBackend; }

    private:
        static std::vector<Scope<MeshSourceBackend>> s_Backends;
        static thread_local const char*              s_LastUsedBackend;
    };

} // namespace Kans
