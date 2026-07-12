#pragma once

#include "Kans3D/Editor/EditorPanel.h"
#include "Kans3D/Asset/Importer/ImportConfig.h"
#include "Kans3D/Asset/Importer/ImportTask.h"
#include "Kans3D/Asset/Importer/MeshSourceBackend.h"

#include <filesystem>
#include <string>
#include <functional>

namespace Kans
{
	// ============================================================
	// ImporterPanel — UE5 风格资产导入面板
	//
	// 功能：
	//   1. 拖放/浏览选择源文件
	//   2. 导入前预览网格统计信息
	//   3. 选择导入后端（Assimp / Auto / 未来扩展）
	//   4. 配置网格、材质、变换等导入选项
	//   5. 异步导入 + 分阶段进度显示
	//   6. 导入完成回调 → EditorLayer 创建 Entity
	// ============================================================

	class ImporterPanel : public EditorPanel
	{
	public:
		ImporterPanel();
		virtual ~ImporterPanel() = default;

		virtual void onImGuiRender(bool isOpen) override;

		// 导入完成回调（EditorLayer 注册）
		using ImportCompleteCallback = std::function<void(const ImportResult&, const ImportConfig&)>;
		void SetImportCompleteCallback(ImportCompleteCallback callback) { m_OnImportComplete = std::move(callback); }

		// 打开面板并加载指定文件
		void OpenWithFile(const std::filesystem::path& filePath);

		// 面板状态
		bool IsImporting() const;
		bool IsPanelOpen() const { return m_IsOpen; }
		void Open()  { m_IsOpen = true; }
		void Close() { m_IsOpen = false; }

	private:
		void DrawHeaderSection();
		void DrawSourceFileSection();
		void DrawPreviewSection();
		void DrawImportSettingsSection();
		void DrawProgressSection();
		void DrawActionButtons();

		void RefreshBackendList();
		void StartImport();
		void CancelImport();
		void OnImportFinished();
		void OpenFileDialog();
		static const char* GetSizeString(uint64_t bytes);

		bool             m_IsOpen = false;
		ImportConfig     m_Config;
		ImportTask       m_Task;
		bool             m_ImportRequested = false;

		MeshSourcePreview m_Preview;
		bool              m_PreviewReady   = false;
		bool              m_PreviewLoading = false;
		std::string       m_PreviewError;

		std::vector<const char*> m_BackendNames;
		int                      m_PreferredBackendIdx = 0;
		char                     m_OutputPathBuffer[512] = {};

		ImportCompleteCallback m_OnImportComplete;
		ImportResult           m_LastResult;
		bool                   m_HasLastResult = false;
	};

} // namespace Kans
