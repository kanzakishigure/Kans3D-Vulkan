#pragma once

#include "ImportConfig.h"
#include "Kans3D/Renderer/Resource/Mesh.h"

#include <atomic>
#include <future>
#include <mutex>
#include <string>
#include <chrono>

namespace Kans
{
	// ============================================================
	// ImportProgress — 导入进度快照（线程安全）
	// ============================================================
	struct ImportProgress
	{
		// 百分比 0.0 ~ 100.0
		float Percentage = 0.0f;

		// 当前阶段描述
		std::string Phase;

		// 当前处理的文件名
		std::string CurrentFile;

		// 各阶段的进度详情
		float ReadProgress    = 0.0f;   // 读取文件 0~100%
		float ParseProgress   = 0.0f;   // 解析数据 0~100%
		float ProcessProgress = 0.0f;   // 后处理   0~100%
		float UploadProgress  = 0.0f;   // GPU 上传 0~100%

		// 统计信息
		uint32_t VerticesDetected  = 0;
		uint32_t TrianglesDetected = 0;
		uint32_t SubMeshesDetected = 0;
		uint32_t MaterialsDetected = 0;

		// 状态标志
		bool IsComplete    = false;
		bool HasError      = false;
		bool IsCancelled   = false;
		std::string ErrorMessage;
		std::string WarningMessage;

		// 耗时（毫秒）
		int64_t ElapsedMs = 0;
	};

	// ============================================================
	// ImportResult — 导入结果
	// ============================================================
	struct ImportResult
	{
		Ref<MeshSource> MeshSource;       // 导入的 MeshSource（成功时非空）
		ImportProgress   FinalProgress;    // 最终进度
		std::vector<std::string> Warnings; // 警告列表
		bool Success = false;
	};

	// ============================================================
	// ImportTask — 异步导入任务
	//
	// 使用 std::async + std::future 实现非阻塞导入。
	// 主线程通过 GetProgress() 轮询进度，通过 Cancel() 取消。
	//
	// 线程安全：Progress 使用 std::atomic + std::mutex 保护
	// ============================================================
	class ImportTask
	{
	public:
		ImportTask() = default;
		~ImportTask();

		// ---- 任务控制 ----

		// 启动异步导入（非阻塞）。如果已有任务在运行，返回 false。
		bool Start(const ImportConfig& config);

		// 请求取消导入。不阻塞，设置取消标志后由工作线程检测。
		void Cancel();

		// 等待导入完成并获取结果（阻塞调用）
		ImportResult WaitForResult();

		// 手动标记为已完成（用于 CpuDone → Completed 转换）
		void MarkCompleted() { m_State.store(State::Completed); }

		// ---- 状态查询（主线程安全调用）----

		bool IsRunning() const;
		bool IsComplete() const;
		ImportProgress GetProgress() const;

		// 运行状态枚举
		enum class State
		{
			Idle,
			Running,
			CpuDone,      // CPU 工作完成，等待主线程创建 GPU 资源
			Completed,
			Cancelled,
			Failed
		};
		State GetState() const;

		// ---- 回调类型 ----
		// 进度更新回调：在工作线程中调用，实现应快速返回
		using ProgressCallback = std::function<void(const ImportProgress&)>;
		void SetProgressCallback(ProgressCallback callback);

	private:
		// 在工作线程中执行的导入逻辑
		ImportResult ExecuteImport(const ImportConfig& config);

		// 更新进度（工作线程调用）
		void UpdateProgress(float readPct, float parsePct, float processPct, float uploadPct,
		                    const char* phase, const std::string& file = "");

		// ---- 成员 ----
		std::future<ImportResult> m_Future;

		mutable std::mutex          m_ProgressMutex;
		ImportProgress              m_Progress;

		std::atomic<State>          m_State{State::Idle};
		std::atomic<bool>           m_CancelRequested{false};

		ProgressCallback            m_ProgressCallback;
		std::chrono::steady_clock::time_point m_StartTime;
	};

} // namespace Kans
