#pragma once

#include "Kans3D/Asset/Importer/ImportConfig.h"
#include "Kans3D/Asset/Importer/MeshSourceBackend.h"
#include "Kans3D/Editor/EditorPanel.h"
#include "Kans3D/Renderer/Resource/Mesh.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <vector>

namespace Kans
{
    // ============================================================
    // ImportProgress — 导入进度快照（线程安全）
    // ============================================================
    struct ImportProgress
    {
        float       Percentage = 0.0f;
        std::string Phase;
        std::string CurrentFile;

        float ReadProgress    = 0.0f;
        float ParseProgress   = 0.0f;
        float ProcessProgress = 0.0f;
        float UploadProgress  = 0.0f;

        uint32_t VerticesDetected  = 0;
        uint32_t TrianglesDetected = 0;
        uint32_t SubMeshesDetected = 0;
        uint32_t MaterialsDetected = 0;

        bool        IsComplete  = false;
        bool        HasError    = false;
        bool        IsCancelled = false;
        std::string ErrorMessage;
        std::string WarningMessage;

        int64_t ElapsedMs = 0;
    };

    // ============================================================
    // LegacyImportResult — 导入结果
    // ============================================================
    struct LegacyImportResult
    {
        Ref<Kans::MeshSource>    MeshSource;
        ImportProgress           FinalProgress;
        std::vector<std::string> Warnings;
        bool                     Success = false;
    };

    // ============================================================
    // ImportJob — CPU 解析和 GPU 上传之间的共享数据
    //
    // 后台线程只解析 CPU 数据；主线程在 future 完成后读取结果并创建 GPU 资源。
    // ============================================================
    struct ImportJob
    {
        ImportConfig       Config;
        LegacyImportResult Result;

        mutable std::mutex                    ProgressMutex;
        ImportProgress                        Progress;
        std::atomic<bool>                     CancelRequested {false};
        std::chrono::steady_clock::time_point StartTime;

        ImportProgress GetProgress() const
        {
            std::lock_guard<std::mutex> lock(ProgressMutex);
            return Progress;
        }

        void UpdateProgress(float              readPct,
                            float              parsePct,
                            float              processPct,
                            float              uploadPct,
                            const char*        phase,
                            const std::string& file = "")
        {
            std::lock_guard<std::mutex> lock(ProgressMutex);
            Progress.ReadProgress    = readPct;
            Progress.ParseProgress   = parsePct;
            Progress.ProcessProgress = processPct;
            Progress.UploadProgress  = uploadPct;
            Progress.Percentage      = readPct * 0.10f + parsePct * 0.40f + processPct * 0.30f + uploadPct * 0.20f;
            Progress.Phase           = phase ? phase : "";
            if (!file.empty())
                Progress.CurrentFile = file;
            auto now           = std::chrono::steady_clock::now();
            Progress.ElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - StartTime).count();
        }
    };

    // ============================================================
    // ImporterPanel — 资产导入面板
    //
    // 每次导入启动一个后台解析任务，主线程轮询并创建 GPU 资源。
    // ============================================================
    class ImporterPanel : public EditorPanel
    {
    public:
        ImporterPanel();
        virtual ~ImporterPanel();

        virtual void onImGuiRender(bool isOpen) override;

        using ImportCompleteCallback = std::function<void(const LegacyImportResult&, const ImportConfig&)>;
        void SetImportCompleteCallback(ImportCompleteCallback callback) { m_OnImportComplete = std::move(callback); }

        void OpenWithFile(const std::filesystem::path& filePath);

        bool IsImporting() const;
        bool IsPanelOpen() const { return m_IsOpen; }
        void Open() { m_IsOpen = true; }
        void Close() { m_IsOpen = false; }

    private:
        void DrawHeaderSection();
        void DrawSourceFileSection();
        void DrawPreviewSection();
        void DrawImportSettingsSection();
        void DrawProgressSection();
        void DrawActionButtons();

        void               RefreshBackendList();
        void               StartImport();
        void               CancelImport();
        void               PollImport();
        void               OpenFileDialog();
        static const char* GetSizeString(uint64_t bytes);

        bool         m_IsOpen = false;
        ImportConfig m_Config;
        bool         m_ImportRequested = false;

        // 只用于当前模型导入，不提供通用任务调度。
        std::future<void> m_CpuImport;
        Ref<ImportJob>    m_SharedJob;

        MeshSourcePreview m_Preview;
        bool              m_PreviewReady   = false;
        bool              m_PreviewLoading = false;
        std::string       m_PreviewError;

        std::vector<const char*> m_BackendNames;
        int                      m_PreferredBackendIdx   = 0;
        char                     m_OutputPathBuffer[512] = {};

        ImportCompleteCallback m_OnImportComplete;
        LegacyImportResult     m_LastResult;
        bool                   m_HasLastResult = false;
    };

} // namespace Kans
