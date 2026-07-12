#include "kspch.h"
#include "ImportTask.h"

#include "MeshSourceImporter.h"
#include "AssimpMeshImporter.h"
#include "AssimpMeshSourceBackend.h"
#include "Kans3D/Renderer/Resource/Mesh.h"

#include <thread>
#include <chrono>

namespace Kans
{
	// ============================================================
	// 析构 — 确保任务结束
	// ============================================================
	ImportTask::~ImportTask()
	{
		Cancel();
		if (m_Future.valid())
		{
			try { m_Future.wait(); }
			catch (...) {}
		}
	}

	// ============================================================
	// Start — 启动异步导入
	// ============================================================
	bool ImportTask::Start(const ImportConfig& config)
	{
		if (m_State.load() == State::Running)
		{
			CORE_WARN("ImportTask::Start — task already running");
			return false;
		}

		if (!config.IsValid())
		{
			CORE_ERROR("ImportTask::Start — invalid config");
			return false;
		}

		// 重置状态
		m_CancelRequested.store(false);
		m_State.store(State::Running);
		m_StartTime = std::chrono::steady_clock::now();

		{
			std::lock_guard<std::mutex> lock(m_ProgressMutex);
			m_Progress = ImportProgress{};
			m_Progress.CurrentFile = config.SourcePath.filename().string();
		}

		// 启动异步任务（复制 config，避免悬空引用）
		m_Future = std::async(std::launch::async,
			[this, config]() -> ImportResult {
				return ExecuteImport(config);
			});

		CORE_INFO("ImportTask — started async import for: {}", config.SourcePath.string());
		return true;
	}

	// ============================================================
	// Cancel — 请求取消
	// ============================================================
	void ImportTask::Cancel()
	{
		if (m_State.load() == State::Running)
		{
			m_CancelRequested.store(true);
			CORE_INFO("ImportTask — cancellation requested");
		}
	}

	// ============================================================
	// WaitForResult — 阻塞等待完成
	// ============================================================
	ImportResult ImportTask::WaitForResult()
	{
		if (!m_Future.valid())
			return ImportResult{};

		try
		{
			return m_Future.get();
		}
		catch (const std::exception& e)
		{
			CORE_ERROR("ImportTask::WaitForResult — exception: {}", e.what());
			ImportResult result;
			result.Success = false;
			result.FinalProgress.ErrorMessage = e.what();
			return result;
		}
	}

	// ============================================================
	// 状态查询
	// ============================================================
	bool ImportTask::IsRunning() const
	{
		return m_State.load() == State::Running;
	}

	bool ImportTask::IsComplete() const
	{
		auto state = m_State.load();
		// CpuDone 不算完成 — GPU 资源还需主线程创建
		return state == State::Completed || state == State::Cancelled || state == State::Failed;
	}

	ImportTask::State ImportTask::GetState() const
	{
		return m_State.load();
	}

	ImportProgress ImportTask::GetProgress() const
	{
		std::lock_guard<std::mutex> lock(m_ProgressMutex);
		return m_Progress;
	}

	void ImportTask::SetProgressCallback(ProgressCallback callback)
	{
		m_ProgressCallback = std::move(callback);
	}

	// ============================================================
	// UpdateProgress — 工作线程调用
	// ============================================================
	void ImportTask::UpdateProgress(float readPct, float parsePct, float processPct, float uploadPct,
	                                 const char* phase, const std::string& file)
	{
		{
			std::lock_guard<std::mutex> lock(m_ProgressMutex);
			m_Progress.ReadProgress    = readPct;
			m_Progress.ParseProgress   = parsePct;
			m_Progress.ProcessProgress = processPct;
			m_Progress.UploadProgress  = uploadPct;

			// 总百分比：加权平均（读 10% + 解析 40% + 处理 30% + 上传 20%）
			m_Progress.Percentage = readPct * 0.10f + parsePct * 0.40f
			                      + processPct * 0.30f + uploadPct * 0.20f;

			m_Progress.Phase = phase ? phase : "";
			if (!file.empty())
				m_Progress.CurrentFile = file;

			auto now = std::chrono::steady_clock::now();
			m_Progress.ElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				now - m_StartTime).count();
		}

		// 回调（在工作线程中调用）
		if (m_ProgressCallback)
		{
			ImportProgress snapshot;
			{
				std::lock_guard<std::mutex> lock(m_ProgressMutex);
				snapshot = m_Progress;
			}
			m_ProgressCallback(snapshot);
		}
	}

	// ============================================================
	// ExecuteImport — 工作线程中的导入逻辑（仅 CPU 工作，无 GL 调用）
	//
	// 方案一：CPU/GPU 分离
	//   - 工作线程：只做 CPU 侧工作（文件读取、Assimp 解析、
	//     stbi_load 图片到内存）
	//   - 完成后进入 State::CpuDone，主线程在 ImGui 渲染前
	//     调用 MeshSource::FinalizeGpuResources() 创建 GL 资源
	// ============================================================
	ImportResult ImportTask::ExecuteImport(const ImportConfig& config)
	{
		ImportResult result;

		// ---- 阶段 1: 校验文件（0-5%）----
		UpdateProgress(0.0f, 0.0f, 0.0f, 0.0f, "Validating file...");
		{
			if (m_CancelRequested.load())
			{
				result.Success = false;
				result.FinalProgress.IsCancelled = true;
				m_State.store(State::Cancelled);
				CORE_INFO("ImportTask — cancelled during validation");
				return result;
			}

			if (!std::filesystem::exists(config.SourcePath))
			{
				result.Success = false;
				result.FinalProgress.ErrorMessage = "File not found: " + config.SourcePath.string();
				m_State.store(State::Failed);
				return result;
			}

			if (std::filesystem::file_size(config.SourcePath) < 64)
			{
				result.Success = false;
				result.FinalProgress.ErrorMessage = "File too small: " + config.SourcePath.string();
				m_State.store(State::Failed);
				return result;
			}

			UpdateProgress(5.0f, 0.0f, 0.0f, 0.0f, "Validation passed");
		}

		// ---- 阶段 2: CPU 解析 + 材质图片加载（5-95%）----
		UpdateProgress(5.0f, 0.0f, 0.0f, 0.0f, "Importing mesh (CPU only)...");
		{
			if (m_CancelRequested.load()) goto cancel;

			// 直接使用 AssimpMeshImporter 的 CPU-only 模式
			AssimpMeshImporter importer(config.SourcePath);
			Ref<MeshSource> meshSource = importer.ImportToMeshSourceCpu();

			UpdateProgress(5.0f, 90.0f, 0.0f, 0.0f, "CPU import complete");

			if (!meshSource)
			{
				result.FinalProgress.ErrorMessage =
					"Mesh processing failed. File may be corrupted or unsupported.";
				result.FinalProgress.HasError = true;
				m_State.store(State::Failed);
				return result;
			}

			result.MeshSource = meshSource;
			const auto& subMeshes = meshSource->GetSubMesh();
			result.FinalProgress.SubMeshesDetected = static_cast<uint32_t>(subMeshes.size());

			for (const auto& sm : subMeshes)
			{
				result.FinalProgress.VerticesDetected  += sm.VertexCount;
				result.FinalProgress.TrianglesDetected += sm.IndexCount;
			}

			if (meshSource->GetMaterialTable())
			{
				result.FinalProgress.MaterialsDetected =
					meshSource->GetMaterialTable()->GetMaterialCount();
			}

			UpdateProgress(5.0f, 95.0f, 0.0f, 0.0f, "Statistics collected");
		}

		// ---- 完成 CPU 阶段（95%，GPU 阶段留给主线程）----
		{
			std::lock_guard<std::mutex> lock(m_ProgressMutex);
			m_Progress.Percentage = 95.0f;
			m_Progress.Phase = "CPU work complete — waiting for GPU upload on main thread";
			auto now = std::chrono::steady_clock::now();
			m_Progress.ElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				now - m_StartTime).count();
		}

		result.Success = true;
		result.FinalProgress = GetProgress();

		// 进入 CpuDone 状态：CPU 工作完成，GPU 资源仍需主线程创建
		m_State.store(State::CpuDone);

		CORE_INFO("ImportTask — CPU work complete: {} ({} submeshes, {} verts, {} tris, {}ms)",
		          config.SourcePath.filename().string(),
		          result.FinalProgress.SubMeshesDetected,
		          result.FinalProgress.VerticesDetected,
		          result.FinalProgress.TrianglesDetected,
		          result.FinalProgress.ElapsedMs);

		return result;

	cancel:
		result.Success = false;
		result.FinalProgress.IsCancelled = true;
		{
			std::lock_guard<std::mutex> lock(m_ProgressMutex);
			result.FinalProgress = m_Progress;
		}
		m_State.store(State::Cancelled);
		return result;
	}

} // namespace Kans
