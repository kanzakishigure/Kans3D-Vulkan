#include "ImporterPanel.h"

#include "Kans3D/Asset/Importer/MeshSourceImporter.h"

#include <imgui.h>

#ifdef _WIN32
#include <commdlg.h>
#endif

namespace Kans
{
	static const ImVec4 kColorPanelBg       = { 0.12f, 0.12f, 0.12f, 1.0f };
	static const ImVec4 kColorSectionBg     = { 0.16f, 0.16f, 0.16f, 1.0f };
	static const ImVec4 kColorAccent        = { 0.24f, 0.58f, 1.0f, 1.0f };
	static const ImVec4 kColorAccentDim     = { 0.18f, 0.44f, 0.76f, 1.0f };
	static const ImVec4 kColorTextPrimary   = { 0.90f, 0.90f, 0.90f, 1.0f };
	static const ImVec4 kColorTextSecondary = { 0.55f, 0.55f, 0.55f, 1.0f };
	static const ImVec4 kColorProgressBg    = { 0.10f, 0.10f, 0.10f, 1.0f };
	static const ImVec4 kColorProgressFill  = { 0.24f, 0.58f, 1.0f, 1.0f };
	static const ImVec4 kColorError         = { 0.95f, 0.30f, 0.25f, 1.0f };
	static const ImVec4 kColorWarning       = { 0.95f, 0.65f, 0.10f, 1.0f };

	ImporterPanel::ImporterPanel()
	{
		RefreshBackendList();
	}

	void ImporterPanel::onImGuiRender(bool isOpen)
	{
		if (!m_IsOpen) return;

		// ═══ 关键：主线程 GPU 资源创建（在 ImGui 渲染之前）═══
		// 工作线程完成 CPU 导入后进入 CpuDone 状态，
		// 此时在主线程（OpenGL context 持有者）创建所有 GL 资源
		if (m_Task.GetState() == ImportTask::State::CpuDone && m_ImportRequested)
		{
			// 获取 CPU-only 导入结果并立即完成 GPU 上传
			ImportResult cpuResult = m_Task.WaitForResult();
			if (cpuResult.Success && cpuResult.MeshSource && !cpuResult.MeshSource->IsGpuReady())
			{
				CORE_INFO("ImporterPanel — finalizing GPU resources on main thread...");
				cpuResult.MeshSource->FinalizeGpuResources();
				m_Task.MarkCompleted();

				// 更新进度到 100%
				{
					ImportProgress finalProgress = cpuResult.FinalProgress;
					finalProgress.Percentage = 100.0f;
					finalProgress.IsComplete = true;
					finalProgress.Phase = "Import complete";
					cpuResult.FinalProgress = finalProgress;
				}

				m_ImportRequested = false;
				m_HasLastResult = true;
				m_LastResult = cpuResult;

				if (m_OnImportComplete)
					m_OnImportComplete(cpuResult, m_Config);
			}
			else
			{
				m_ImportRequested = false;
				m_HasLastResult = true;
				m_LastResult = cpuResult;
			}
		}
		else if (m_Task.IsComplete() && m_ImportRequested)
		{
			m_ImportRequested = false;
			OnImportFinished();
		}

		ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_FirstUseEver);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, kColorPanelBg);

		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse;
		bool panelOpen = true;
		if (ImGui::Begin("Import Asset", &panelOpen, flags))
		{
			if (!panelOpen) m_IsOpen = false;

			DrawHeaderSection();
			ImGui::Separator();
			DrawSourceFileSection();
			ImGui::Spacing();

			ImGui::Columns(2, "ImportColumns", false);
			ImGui::SetColumnWidth(0, 260);

			DrawPreviewSection();
			ImGui::NextColumn();
			DrawImportSettingsSection();

			ImGui::Columns(1);
			ImGui::Separator();

			DrawProgressSection();
			DrawActionButtons();
		}
		ImGui::End();
		ImGui::PopStyleColor(1);

		if (!panelOpen) m_IsOpen = false;
	}

	void ImporterPanel::DrawHeaderSection()
	{
		ImGui::PushStyleColor(ImGuiCol_Text, kColorTextPrimary);
		ImGui::TextWrapped("Import Asset");
		ImGui::PopStyleColor();
		ImGui::SameLine();
		ImGui::PushStyleColor(ImGuiCol_Text, kColorTextSecondary);
		ImGui::TextWrapped("- Choose import settings and options");
		ImGui::PopStyleColor();
	}

	void ImporterPanel::DrawSourceFileSection()
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, kColorSectionBg);
		ImGui::BeginChild("SourceSection", ImVec2(0, 40), true);

		ImGui::PushStyleColor(ImGuiCol_Text, kColorTextSecondary);
		ImGui::TextUnformatted("Source File:");
		ImGui::PopStyleColor();
		ImGui::SameLine();

		std::string pathDisplay = m_Config.SourcePath.empty()
			? "Drop a file here or click Browse..."
			: m_Config.SourcePath.string();

		ImGui::PushStyleColor(ImGuiCol_Text, kColorTextPrimary);
		ImGui::TextUnformatted(pathDisplay.c_str());
		ImGui::PopStyleColor();

		ImGui::SameLine(ImGui::GetWindowWidth() - 120);
		if (ImGui::Button("Browse...", ImVec2(80, 0)))
			OpenFileDialog();

		if (!m_Config.SourcePath.empty() && std::filesystem::exists(m_Config.SourcePath))
		{
			uintmax_t sz = std::filesystem::file_size(m_Config.SourcePath);
			ImGui::SameLine();
			ImGui::PushStyleColor(ImGuiCol_Text, kColorTextSecondary);
			ImGui::TextUnformatted(GetSizeString(sz));
			ImGui::PopStyleColor();
		}

		ImGui::EndChild();
		ImGui::PopStyleColor(1);
	}

	void ImporterPanel::DrawPreviewSection()
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, kColorSectionBg);

		ImGui::BeginChild("PreviewThumbnail", ImVec2(0, 180), true);
		{
			ImVec2 region = ImGui::GetContentRegionAvail();
			if (m_PreviewReady)
			{
				ImVec2 center = ImGui::GetCursorScreenPos();
				center.x += (region.x - 64) * 0.5f;
				center.y += (region.y - 64) * 0.5f - 10;
				ImGui::SetCursorScreenPos(center);
				ImDrawList* draw = ImGui::GetWindowDrawList();
				draw->AddRectFilled(center, { center.x + 64, center.y + 64 }, IM_COL32(40, 60, 100, 255), 6);
				draw->AddRect(center, { center.x + 64, center.y + 64 }, IM_COL32(80, 120, 200, 255), 6, 0, 2);
				ImVec2 textPos = { center.x + 8, center.y + 20 };
				ImGui::SetCursorScreenPos(textPos);
				ImGui::PushStyleColor(ImGuiCol_Text, kColorTextPrimary);
				ImGui::TextUnformatted(m_Preview.FormatName.c_str());
				ImGui::PopStyleColor();
			}
			else if (m_PreviewLoading)
			{
				ImVec2 pos = { ImGui::GetCursorPosX() + (region.x - 200) * 0.5f, ImGui::GetCursorPosY() + (region.y - 30) * 0.5f };
				ImGui::SetCursorScreenPos(pos);
				ImGui::PushStyleColor(ImGuiCol_Text, kColorTextSecondary);
				ImGui::TextUnformatted("Loading preview...");
				ImGui::PopStyleColor();
			}
			else
			{
				ImVec2 pos = { ImGui::GetCursorPosX() + (region.x - 200) * 0.5f, ImGui::GetCursorPosY() + (region.y - 30) * 0.5f };
				ImGui::SetCursorScreenPos(pos);
				ImGui::PushStyleColor(ImGuiCol_Text, kColorTextSecondary);
				ImGui::TextUnformatted("No file selected");
				ImGui::PopStyleColor();
			}
		}
		ImGui::EndChild();
		ImGui::Spacing();

		ImGui::BeginChild("StatsTable", ImVec2(0, 200), true);
		{
			auto StatRow = [](const char* label, uint32_t value, bool highlight = false) {
				ImGui::PushStyleColor(ImGuiCol_Text, kColorTextSecondary);
				ImGui::TextUnformatted(label);
				ImGui::PopStyleColor();
				ImGui::SameLine(140);
				ImGui::PushStyleColor(ImGuiCol_Text, highlight ? kColorAccent : kColorTextPrimary);
				ImGui::Text("%u", value);
				ImGui::PopStyleColor();
			};

			if (m_PreviewReady)
			{
				StatRow("Vertices:",  m_Preview.VertexCount);
				StatRow("Triangles:", m_Preview.TriangleCount);
				StatRow("SubMeshes:", m_Preview.SubMeshCount, true);
				StatRow("Materials:", m_Preview.MaterialCount, m_Preview.MaterialCount > 0);
				StatRow("Lights:",    m_Preview.LightCount);
				StatRow("Cameras:",   m_Preview.CameraCount);
				if (m_Preview.AnimationCount > 0)
					StatRow("Animations:", m_Preview.AnimationCount, true);
				if (m_Preview.HasEmbeddedTextures)
				{
					ImGui::Spacing();
					ImGui::PushStyleColor(ImGuiCol_Text, kColorWarning);
					ImGui::TextWrapped("* Contains embedded textures");
					ImGui::PopStyleColor();
				}
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, kColorTextSecondary);
				ImGui::TextWrapped("File info will appear here\nafter selecting a source file.");
				ImGui::PopStyleColor();
			}

			if (!m_PreviewError.empty())
			{
				ImGui::Spacing();
				ImGui::PushStyleColor(ImGuiCol_Text, kColorError);
				ImGui::TextWrapped("Error: %s", m_PreviewError.c_str());
				ImGui::PopStyleColor();
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor(1);
	}

	void ImporterPanel::DrawImportSettingsSection()
	{
		ImGui::PushStyleColor(ImGuiCol_ChildBg, kColorSectionBg);
		ImGui::BeginChild("SettingsPanel", ImVec2(0, 0), false);
		{
			ImGui::PushStyleColor(ImGuiCol_Text, kColorTextPrimary);
			ImGui::TextUnformatted("Backend");
			ImGui::PopStyleColor();
			ImGui::SameLine(100);
			ImGui::SetNextItemWidth(250);
			if (ImGui::Combo("##BackendCombo", &m_PreferredBackendIdx,
				m_BackendNames.data(), (int)m_BackendNames.size()))
			{
				m_Config.bAutoSelectBackend = (m_PreferredBackendIdx == 0);
				m_Config.SelectedBackendIndex = m_PreferredBackendIdx;
			}
			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Text, kColorTextPrimary);
			ImGui::TextUnformatted("Output Dir");
			ImGui::PopStyleColor();
			ImGui::SameLine(100);
			ImGui::SetNextItemWidth(250);
			ImGui::InputText("##OutputPath", m_OutputPathBuffer, sizeof(m_OutputPathBuffer),
				ImGuiInputTextFlags_ReadOnly);
			ImGui::SameLine();
			if (ImGui::Button("...", ImVec2(25, 0))) { /* TODO: folder dialog */ }
			ImGui::Spacing();

			ImGui::PushStyleColor(ImGuiCol_Text, kColorTextPrimary);
			ImGui::TextUnformatted("Asset Name");
			ImGui::PopStyleColor();
			ImGui::SameLine(100);
			ImGui::SetNextItemWidth(250);
			char nameBuf[256];
			strncpy_s(nameBuf, m_Config.AssetName.c_str(), sizeof(nameBuf));
			if (ImGui::InputText("##AssetName", nameBuf, sizeof(nameBuf)))
				m_Config.AssetName = nameBuf;
			ImGui::Spacing();
			ImGui::Separator();

			if (ImGui::CollapsingHeader("Mesh", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Indent(10);
				ImGui::Checkbox("Import Materials", &m_Config.bImportMaterials);
				ImGui::Checkbox("Import Textures", &m_Config.bImportTextures);
				ImGui::Checkbox("Import Lights", &m_Config.bImportLights);
				ImGui::Checkbox("Import Cameras", &m_Config.bImportCameras);
				ImGui::Checkbox("Generate Lightmap UVs", &m_Config.bGenerateLightmapUVs);
				ImGui::Checkbox("Combine Meshes", &m_Config.bCombineMeshes);
				ImGui::Checkbox("Remove Degenerates", &m_Config.bRemoveDegenerates);
				ImGui::Unindent(10);
			}

			if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
			{
				ImGui::Indent(10);
				ImGui::Checkbox("Create Material Assets", &m_Config.bCreateMaterialAssets);
				ImGui::Checkbox("Search Existing Materials", &m_Config.bSearchExistingMaterials);
				ImGui::PushStyleColor(ImGuiCol_Text, kColorTextSecondary);
				ImGui::TextUnformatted("Search Path:");
				ImGui::PopStyleColor();
				ImGui::SameLine();
				ImGui::SetNextItemWidth(150);
				char buf[256];
				strncpy_s(buf, m_Config.MaterialSearchPath.c_str(), sizeof(buf));
				if (ImGui::InputText("##MatSearchPath", buf, sizeof(buf)))
					m_Config.MaterialSearchPath = buf;
				ImGui::Unindent(10);
			}

			if (ImGui::CollapsingHeader("Transform"))
			{
				ImGui::Indent(10);
				ImGui::Checkbox("Apply Root Transform", &m_Config.bApplyRootTransform);
				ImGui::PushStyleColor(ImGuiCol_Text, kColorTextSecondary);
				ImGui::TextUnformatted("Uniform Scale:");
				ImGui::PopStyleColor();
				ImGui::SameLine(100);
				ImGui::SetNextItemWidth(100);
				ImGui::DragFloat("##UniformScale", &m_Config.ImportUniformScale, 0.01f, 0.01f, 100.0f, "%.2f");
				ImGui::Unindent(10);
			}
		}
		ImGui::EndChild();
		ImGui::PopStyleColor(1);
	}

	void ImporterPanel::DrawProgressSection()
	{
		if (!m_Task.IsRunning() && m_Task.GetState() != ImportTask::State::CpuDone) return;
		ImportProgress progress = m_Task.GetProgress();
		ImGui::Spacing();
		ImVec2 region = ImGui::GetContentRegionAvail();

		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kColorProgressFill);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, kColorProgressBg);
		char overlay[64];
		sprintf_s(overlay, "%.0f%%", progress.Percentage);
		ImGui::ProgressBar(progress.Percentage / 100.0f, ImVec2(region.x - 180, 18), overlay);
		ImGui::PopStyleColor(2);
		ImGui::SameLine();

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.15f, 0.15f, 1.0f));
		if (ImGui::Button("Cancel", ImVec2(80, 0))) CancelImport();
		ImGui::PopStyleColor(1);

		ImGui::PushStyleColor(ImGuiCol_Text, kColorTextSecondary);
		ImGui::TextWrapped("%s  |  %s  |  %.1fs",
			progress.Phase.c_str(), progress.CurrentFile.c_str(), progress.ElapsedMs / 1000.0f);
		ImGui::PopStyleColor();

		ImGui::Spacing();
		auto MiniBar = [](const char* label, float pct) {
			ImGui::PushStyleColor(ImGuiCol_Text, kColorTextSecondary);
			ImGui::Text("%s", label);
			ImGui::PopStyleColor();
			ImGui::SameLine(60);
			ImGui::SetNextItemWidth(120);
			char buf[32];
			sprintf_s(buf, "%.0f%%", pct);
			ImGui::ProgressBar(pct / 100.0f, ImVec2(120, 10), buf);
			ImGui::SameLine();
		};
		MiniBar("Read:",  progress.ReadProgress);
		MiniBar("Parse:", progress.ParseProgress);
		MiniBar("Proc:",  progress.ProcessProgress);
		MiniBar("GPU:",   progress.UploadProgress);
	}

	void ImporterPanel::DrawActionButtons()
	{
		ImGui::Spacing();
		float availW = ImGui::GetContentRegionAvail().x;
		float btnW = 100, pad = 10;
		float totalW = btnW * 2 + pad;
		ImGui::SetCursorPosX(availW - totalW - 20);

		bool canImport = m_Config.IsValid() && !m_Task.IsRunning();
		if (!canImport)
		{
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
			ImGui::Button("Import", ImVec2(btnW, 30));
			ImGui::PopStyleVar();
		}
		else
		{
			ImGui::PushStyleColor(ImGuiCol_Button, kColorAccentDim);
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kColorAccent);
			if (ImGui::Button("Import", ImVec2(btnW, 30))) StartImport();
			ImGui::PopStyleColor(2);
		}
		ImGui::SameLine(0, pad);
		if (ImGui::Button("Close", ImVec2(btnW, 30))) m_IsOpen = false;
	}

	void ImporterPanel::OpenFileDialog()
	{
#ifdef _WIN32
		OPENFILENAMEA ofn = {};
		char szFile[512] = {};
		ofn.lStructSize = sizeof(ofn);
		ofn.hwndOwner = nullptr;
		ofn.lpstrFile = szFile;
		ofn.nMaxFile = sizeof(szFile);
		ofn.lpstrFilter =
			"3D Model Files\0*.fbx;*.obj;*.gltf;*.glb;*.dae;*.ply;*.stl;*.3ds;*.blend;*.x3d\0"
			"All Supported\0*.fbx;*.obj;*.gltf;*.glb;*.dae;*.ply;*.stl;*.3ds;*.blend;*.x3d;*.lwo;*.ase\0"
			"All Files\0*.*\0";
		ofn.nFilterIndex = 1;
		ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
		if (GetOpenFileNameA(&ofn))
			OpenWithFile(std::filesystem::path(szFile));
#endif
	}

	void ImporterPanel::OpenWithFile(const std::filesystem::path& filePath)
	{
		m_Config = ImportConfig::FromFile(filePath,
			std::filesystem::current_path() / "Assets" / "MeshSources");
		m_IsOpen = true;
		strncpy_s(m_OutputPathBuffer, m_Config.OutputDirectory.string().c_str(),
		          sizeof(m_OutputPathBuffer) - 1);

		m_PreviewReady   = false;
		m_PreviewLoading = true;
		m_PreviewError.clear();

		m_Preview = MeshSourceImporter::PreviewMeshSource(filePath);
		m_PreviewReady   = m_Preview.IsValid();
		m_PreviewLoading = false;
		if (!m_PreviewReady)
			m_PreviewError = "Could not preview this file.";

		RefreshBackendList();
		m_PreferredBackendIdx = 0;
		CORE_INFO("ImporterPanel — opened with file: {}", filePath.string());
	}

	void ImporterPanel::RefreshBackendList()
	{
		m_BackendNames = MeshSourceImporter::GetBackendNames();
	}

	void ImporterPanel::StartImport()
	{
		if (!m_Config.IsValid()) return;
		m_Config.OutputDirectory = m_OutputPathBuffer;
		m_Config.bAutoSelectBackend = (m_PreferredBackendIdx == 0);
		m_Config.SelectedBackendIndex = m_PreferredBackendIdx;

		if (m_Task.Start(m_Config))
		{
			m_ImportRequested = true;
			m_HasLastResult = false;
		}
	}

	void ImporterPanel::CancelImport()
	{
		m_Task.Cancel();
		m_ImportRequested = false;
	}

	void ImporterPanel::OnImportFinished()
	{
		m_ImportRequested = false;
		m_HasLastResult = true;
		m_LastResult = m_Task.WaitForResult();

		if (m_LastResult.Success && m_OnImportComplete)
			m_OnImportComplete(m_LastResult, m_Config);
	}

	bool ImporterPanel::IsImporting() const { return m_Task.IsRunning(); }

	const char* ImporterPanel::GetSizeString(uint64_t bytes)
	{
		static char buf[32];
		const char* units[] = { "B", "KB", "MB", "GB", "TB" };
		int idx = 0;
		double size = (double)bytes;
		while (size >= 1024.0 && idx < 4) { size /= 1024.0; ++idx; }
		sprintf_s(buf, "(%d %s)", (int)size, units[idx]);
		return buf;
	}

} // namespace Kans
