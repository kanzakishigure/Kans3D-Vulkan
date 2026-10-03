#include "kspch.h"
#include "MeshSourceImporter.h"

#include "Kans3D/Renderer/Resource/Mesh.h"

namespace Kans
{
    std::vector<Scope<MeshSourceBackend>> MeshSourceImporter::s_Backends;
    thread_local const char*              MeshSourceImporter::s_LastUsedBackend = nullptr;

    void MeshSourceImporter::RegisterBackend(Scope<MeshSourceBackend> backend)
    {
        if (!backend)
        {
            CORE_ERROR("MeshSourceImporter::RegisterBackend — backend is null");
            return;
        }
        CORE_INFO("MeshSourceImporter — registered backend: {}", backend->GetName());
        s_Backends.insert(s_Backends.begin(), std::move(backend));
    }

    bool MeshSourceImporter::UnregisterBackend(const char* name)
    {
        auto it = std::find_if(s_Backends.begin(), s_Backends.end(), [name](const Scope<MeshSourceBackend>& b) {
            return std::strcmp(b->GetName(), name) == 0;
        });
        if (it != s_Backends.end())
        {
            CORE_INFO("MeshSourceImporter — unregistered backend: {}", name);
            s_Backends.erase(it);
            return true;
        }
        return false;
    }

    std::vector<const char*> MeshSourceImporter::GetBackendNames()
    {
        std::vector<const char*> names;
        names.reserve(s_Backends.size());
        names.push_back("Auto (Best Match)");
        for (auto& b : s_Backends)
            names.push_back(b->GetName());
        return names;
    }

    Ref<MeshSource> MeshSourceImporter::ImportMeshSource(const std::filesystem::path& filePath,
                                                         BackendProgressCallback      progress,
                                                         int                          preferredBackendIndex)
    {
        if (filePath.empty() || !std::filesystem::exists(filePath))
        {
            CORE_ERROR("MeshSourceImporter::ImportMeshSource — file not found: {}", filePath.string());
            return nullptr;
        }

        const auto& ext = filePath.extension();

        // 选择项的索引从 1 开始，转换为后端数组下标后优先尝试。
        if (preferredBackendIndex > 0 && preferredBackendIndex <= static_cast<int>(s_Backends.size()))
        {
            auto& backend = s_Backends[preferredBackendIndex - 1];
            if (backend->Supports(ext))
            {
                CORE_INFO("MeshSourceImporter — using preferred backend '{}'", backend->GetName());
                Ref<MeshSource> result = backend->Import(filePath, progress);
                if (result)
                {
                    s_LastUsedBackend = backend->GetName();
                    return result;
                }
            }
        }

        // 未得到导入结果时，按列表顺序尝试支持该扩展名的后端。
        for (auto& backend : s_Backends)
        {
            if (!backend->Supports(ext))
                continue;

            CORE_INFO(
                "MeshSourceImporter — using backend '{}' for: {}", backend->GetName(), filePath.filename().string());

            Ref<MeshSource> result = backend->Import(filePath, progress);
            if (result)
            {
                s_LastUsedBackend = backend->GetName();
                return result;
            }

            CORE_WARN("MeshSourceImporter — backend '{}' reported support but failed: {}",
                      backend->GetName(),
                      filePath.string());
        }

        CORE_ERROR("MeshSourceImporter::ImportMeshSource — no backend supports: {}", filePath.string());
        return nullptr;
    }

    bool MeshSourceImporter::CanImport(const std::filesystem::path& filePath)
    {
        if (filePath.empty() || !std::filesystem::exists(filePath))
            return false;

        const auto& ext = filePath.extension();
        for (auto& backend : s_Backends)
        {
            if (backend->Supports(ext) && backend->TryLoad(filePath))
                return true;
        }
        return false;
    }

    MeshSourcePreview MeshSourceImporter::PreviewMeshSource(const std::filesystem::path& filePath)
    {
        if (filePath.empty() || !std::filesystem::exists(filePath))
            return MeshSourcePreview {};

        const auto& ext = filePath.extension();
        for (auto& backend : s_Backends)
        {
            if (backend->Supports(ext) && backend->SupportsPreview())
            {
                MeshSourcePreview preview = backend->Preview(filePath);
                if (preview.IsValid())
                    return preview;
            }
        }
        return MeshSourcePreview {};
    }

} // namespace Kans
