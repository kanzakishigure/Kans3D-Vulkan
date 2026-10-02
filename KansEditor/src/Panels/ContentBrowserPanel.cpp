#include "ContentBrowserPanel.h"
#include "ContentBrowserLayout.h"
#include "Kans3D/ImGui/KansUI.h"
#include "Kans3D/ImGui/Colors.h"
#include "Kans3D/Core/Hash.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <stb_image.h>

#define ADJUST_CONTRNTBROWSER false

namespace Kans
{
	ContentBrowserPanel* ContentBrowserPanel::s_Instance = nullptr;

	// ---- tiny helpers (file-size / date formatting for item metadata) ----
	static std::string FormatFileSize(uint64_t bytes)
	{
		const char* units[] = { "B", "KB", "MB", "GB", "TB" };
		int idx = 0;
		double size = static_cast<double>(bytes);
		while (size >= 1024.0 && idx < 4) { size /= 1024.0; ++idx; }
		std::ostringstream oss;
		if (idx == 0)
			oss << bytes << ' ' << units[idx];
		else
			oss << std::fixed << std::setprecision(1) << size << ' ' << units[idx];
		return oss.str();
	}

	static std::string FormatFileDate(const std::filesystem::file_time_type& ftime)
	{
		auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
			ftime - std::filesystem::file_time_type::clock::now() +
			std::chrono::system_clock::now());
		std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
		std::tm tm_buf = *std::localtime(&tt);
		std::ostringstream oss;
		oss << std::put_time(&tm_buf, "%Y-%m-%d");
		return oss.str();
	}

	// ------------------------------------------------------------------
	ContentBrowserPanel::ContentBrowserPanel()
		:m_CurrentPath(KansFileSystem::GetAssetFolder().parent_path())
	{
		s_Instance = this;
		m_ContentBrowserItemList.clear();
	}

	static std::string LowerExtension(const std::filesystem::path& path)
	{
		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return ext;
	}

	void ContentBrowserPanel::OnItemClicked(const std::filesystem::path& path,
		bool isDirectory, bool doubleClick)
	{
		SelectItem(path, isDirectory);
		if (!doubleClick)
			return;
		if (isDirectory)
		{
			m_CurrentPath = path;
			NeedRefresh = true;
			return;
		}
		// File editors are registered here when they become available.
		m_PreviewMessage = "No editor is registered for this file type.";
	}

	void ContentBrowserPanel::SelectItem(const std::filesystem::path& path, bool isDirectory)
	{
		if (m_SelectedPath == path)
			return;
		m_SelectedPath = path;
		m_PreviewTexture.reset();
		m_PreviewText.clear();
		m_PreviewMessage.clear();
		m_PreviewType = PreviewType::Other;
		m_MaterialColor = ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
		if (isDirectory)
		{
			m_PreviewType = PreviewType::Folder;
			return;
		}

		const std::string ext = LowerExtension(path);
		if (ext == ".mat" || ext == ".material" || ext == ".mtl")
		{
			m_PreviewType = PreviewType::Material;
			if (ext == ".mtl")
			{
				m_PreviewMessage = "Showing diffuse color; textures and shader lighting are not rendered.";
				std::ifstream file(path);
				std::string token;
				while (file >> token)
				{
					if (token == "Kd")
					{
						float r, g, b;
						if (file >> r >> g >> b)
								m_MaterialColor = ImVec4(std::clamp(r, 0.0f, 1.0f),
									std::clamp(g, 0.0f, 1.0f), std::clamp(b, 0.0f, 1.0f), 1.0f);
						break;
					}
				}
			}
			else
				m_PreviewMessage = "Material format has no preview loader yet; showing a neutral sphere.";
			return;
		}

		static const std::unordered_set<std::string> imageExtensions = {
			".png", ".jpg", ".jpeg", ".bmp", ".tga", ".hdr", ".gif"};
		if (imageExtensions.count(ext))
		{
			m_PreviewType = PreviewType::Image;
			int width = 0, height = 0, channels = 0;
			if (stbi_info(path.string().c_str(), &width, &height, &channels) &&
				width > 0 && height > 0 && (channels == 3 || channels == 4))
			{
				TextureSpecification spec;
				m_PreviewTexture = Texture2D::Create(spec, path);
			}
			else
				m_PreviewMessage = "This image cannot be previewed.";
			return;
		}

		static const std::unordered_set<std::string> textExtensions = {
			".txt", ".md", ".json", ".yaml", ".yml", ".xml", ".ini",
			".csv", ".glsl", ".shader", ".hlsl", ".vert", ".frag",
			".geom", ".comp", ".h", ".hpp", ".c", ".cpp", ".cs",
			".py", ".lua", ".toml", ".cmake"};
		const bool knownText = textExtensions.count(ext) != 0;
		std::ifstream file(path, std::ios::binary);
		if (file)
		{
			m_PreviewText.resize(64 * 1024);
			file.read(m_PreviewText.data(), m_PreviewText.size());
			m_PreviewText.resize(static_cast<size_t>(file.gcount()));
			const bool binary = std::any_of(m_PreviewText.begin(), m_PreviewText.end(),
				[](unsigned char c) { return c == 0 || (c < 32 && c != '\n' && c != '\r' && c != '\t'); });
			if (!binary && (!m_PreviewText.empty() || knownText))
			{
				m_PreviewType = PreviewType::Text;
				if (file.peek() != EOF)
					m_PreviewMessage = "Showing the first 64 KB.";
				return;
			}
			m_PreviewText.clear();
		}
		m_PreviewMessage = knownText ? "This text file cannot be previewed."
			: "No preview is available for this file type.";
	}

	void ContentBrowserPanel::DrawPreview()
	{
		ImGui::BeginChild("AssetPreview", { 0, 0 }, false);
		if (m_SelectedPath.empty())
		{
			ImGui::TextWrapped("Select a file or folder to preview it.");
			ImGui::EndChild();
			return;
		}
		ImGui::TextWrapped("%s", m_SelectedPath.filename().string().c_str());
		ImGui::TextDisabled("%s", m_SelectedPath.extension().string().c_str());
		ImGui::Separator();
		if (m_PreviewType == PreviewType::Image && m_PreviewTexture)
		{
			const float w = static_cast<float>(m_PreviewTexture->GetWidth());
			const float h = static_cast<float>(m_PreviewTexture->GetHeight());
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const float scale = std::min({1.0f, avail.x / w, std::max(1.0f, avail.y - 55.0f) / h});
			ImGui::Image((ImTextureID)(uintptr_t)m_PreviewTexture->GetRenererID(),
				{w * scale, h * scale}, {0, 1}, {1, 0});
			ImGui::TextDisabled("%u x %u", m_PreviewTexture->GetWidth(), m_PreviewTexture->GetHeight());
		}
		else if (m_PreviewType == PreviewType::Material)
		{
			const float diameter = std::min(190.0f, ImGui::GetContentRegionAvail().x - 12.0f);
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			const ImVec2 center(origin.x + diameter * 0.5f, origin.y + diameter * 0.5f);
			ImDrawList* draw = ImGui::GetWindowDrawList();
			for (int ring = 20; ring >= 1; --ring)
			{
				const float t = static_cast<float>(ring) / 20.0f;
				const float light = 0.26f + 0.90f * (1.0f - t);
				const ImVec4 color(std::min(1.0f, m_MaterialColor.x * light),
					std::min(1.0f, m_MaterialColor.y * light),
					std::min(1.0f, m_MaterialColor.z * light), 1.0f);
				draw->AddCircleFilled({center.x - diameter * 0.08f * (1.0f - t),
					center.y - diameter * 0.08f * (1.0f - t)}, diameter * 0.5f * t,
					ImGui::ColorConvertFloat4ToU32(color), 64);
			}
			ImGui::Dummy({diameter, diameter});
		}
		else if (m_PreviewType == PreviewType::Text && !m_PreviewText.empty())
			ImGui::InputTextMultiline("##TextPreview", m_PreviewText.data(),
				m_PreviewText.size() + 1, {-1, -1}, ImGuiInputTextFlags_ReadOnly);
		else if (m_PreviewType == PreviewType::Folder)
			ImGui::TextDisabled("Folder - double-click to enter");
		if (!m_PreviewMessage.empty())
			ImGui::TextWrapped("%s", m_PreviewMessage.c_str());
		ImGui::EndChild();
	}

	// ------------------------------------------------------------------
	// Choose a tier from the current viewport and grid dimensions.
	// ------------------------------------------------------------------
	void ContentBrowserPanel::CalculateLayout(float gridWidth, float gridHeight,
		float viewportWidth, float viewportHeight)
	{
		const auto layout = ContentBrowserLayout::Calculate(gridWidth, gridHeight,
			viewportWidth, viewportHeight, ImGui::GetTextLineHeight());
		m_CurrentIconSize = layout.IconSize;
		m_ComputedColumns = layout.Columns;

		// ---- Map discrete tier -- view mode ----
		if      (m_CurrentIconSize <= 64.0f)  m_CurrentViewMode = ViewMode::CompactGrid;
		else if (m_CurrentIconSize <= 96.0f)  m_CurrentViewMode = ViewMode::StandardGrid;
		else if (m_CurrentIconSize <= 128.0f) m_CurrentViewMode = ViewMode::DetailedGrid;
		else                                  m_CurrentViewMode = ViewMode::ExpandedGrid;
	}

	// ---- Draw a thin horizontal separator line ----
	static void DrawThinSeparator(ImColor color, float thickness = 1.0f)
	{
		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImVec2 p = ImGui::GetCursorScreenPos();
		float w = ImGui::GetContentRegionAvail().x;
		draw->AddRectFilled(p, { p.x + w, p.y + thickness }, color);
		ImGui::Dummy({ 0, thickness + 2.0f });
	}

	// ------------------------------------------------------------------
	void ContentBrowserPanel::onImGuiRender(bool isOpen)
	{
		ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
		const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
		ImGui::SetNextWindowSizeConstraints(
			{ std::min(700.0f, displaySize.x * 0.9f), displaySize.y * 0.20f },
			{ FLT_MAX, FLT_MAX });
		ImGui::Begin("ContentBrowser", nullptr, windowFlags);
		const ImVec2 viewportSize = ImGui::GetWindowViewport()->Size;

		// ---- Background colors for  panels ----
		const ImVec4 colSourcePanel = ImGui::ColorConvertU32ToFloat4(IM_COL32(24, 24, 24, 255));    // very dark source panel
		const ImVec4 colAssetPanel  = ImGui::ColorConvertU32ToFloat4(IM_COL32(32, 32, 32, 255));    // slightly lighter asset area
		const ImVec4 colBreadcrumb  = ImGui::ColorConvertU32ToFloat4(IM_COL32(20, 20, 20, 255));    // breadcrumb bar background
		const ImVec4 colBorder      = ImGui::ColorConvertU32ToFloat4(IM_COL32(50, 50, 50, 255));    // subtle borders
		const ImVec4 colHeaderText  = ImGui::ColorConvertU32ToFloat4(Colors::Theme::gray_2);

#if ADJUST_CONTRNTBROWSER
		{
			ImGui::SliderFloat("FramePadding",      &FramePadding,    0, 100);
			ImGui::SliderFloat("FrameBorderSize",   &FrameBorderSize, -100, 100);
			Kans::UI::DrawVec2Control("outerSize",  OuterSize);
			Kans::UI::DrawVec2Control("ItemInnerSpacing", ItemInnerSpacing);
			ImGui::SliderFloat("ImageButtonSize",   &m_CurrentIconSize, 32, 198);
			ImGui::SliderFloat("FrameRounding",     &FrameRounding,   0, 100);
		}
#endif

		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

		// ---- Outer split table: left = source panel, right = asset panel ----
		{
			static ImGuiTableFlags flags =
				ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
				ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX |
				ImGuiTableFlags_SizingStretchProp;

			ImVec2 tableSize = ImGui::GetWindowSize();
			tableSize.y *= 0.94f;

			// Subtle  vertical separator
		ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
		if (ImGui::BeginTable("ContentBrowserSplit", 3, flags, { 0, tableSize.y }))
			{
				// ---- Column setup with widths ----
				ImGui::TableSetupColumn(
					KansFileSystem::GetAssetFolder().filename().string().c_str(),
					ImGuiTableColumnFlags_NoHeaderLabel | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthFixed,
					200.0f);
				ImGui::TableSetupColumn("Assets", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthFixed, 260.0f);

				// ----------------------------------------------------------------------------------------------------------------
				//  ROW 1: column headers (breadcrumb row)
				// ----------------------------------------------------------------------------------------------------------------
				ImGui::TableNextRow(ImGuiTableRowFlags_Headers, 28.0f);

				// --- Left column header: "Sources" label ---
				{
					ImGui::TableSetColumnIndex(0);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImColor(colSourcePanel));
					const char* cn = ImGui::TableGetColumnName(0);
					ImGui::PushID(cn);
					ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
					ImGui::TextUnformatted("  Sources");
					ImGui::PopStyleColor(2);
					ImGui::PopID();
				}

				// --- Right column header: breadcrumb toolbar ---
				{
					ImGui::TableSetColumnIndex(1);
					ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImColor(colBreadcrumb));
					const char* cn = ImGui::TableGetColumnName(1);
					ImGui::PushID(cn);

					// Breadcrumb path background
					ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
					{
						std::filesystem::path workspaceRoot = KansFileSystem::GetAssetFolder().parent_path();

						// Build a list of segments from workspaceRoot to m_CurrentPath
						std::vector<std::pair<std::string, std::filesystem::path>> crumbs;

						std::string rootName = workspaceRoot.filename().string();
						if (rootName.empty()) rootName = workspaceRoot.string();
						crumbs.push_back({ rootName, workspaceRoot });

						if (m_CurrentPath != workspaceRoot)
						{
							std::vector<std::string> revSegments;
							std::filesystem::path walk = m_CurrentPath;
							while (walk != workspaceRoot && walk.has_parent_path())
							{
								revSegments.push_back(walk.filename().string());
								walk = walk.parent_path();
							}

							std::filesystem::path accumulated = workspaceRoot;
							for (auto it = revSegments.rbegin(); it != revSegments.rend(); ++it)
							{
								accumulated /= *it;
								crumbs.push_back({ *it, accumulated });
							}
						}

						// Render breadcrumbs
						{
							float availW = ImGui::GetContentRegionAvail().x;
							const float sepSpacing = 4.0f;
							const float sepTextW    = ImGui::CalcTextSize("/").x;
							const float gapW        = sepTextW + sepSpacing;
							const float ellipsisW   = ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2.0f;

							size_t lastIdx = crumbs.size() - 1;
							std::vector<float> crumbW(crumbs.size());
							float totalW = 0.0f;
							for (size_t i = 0; i < crumbs.size(); ++i)
							{
								if (i == lastIdx)
									crumbW[i] = ImGui::CalcTextSize(crumbs[i].first.c_str()).x;
								else
									crumbW[i] = ImGui::CalcTextSize(crumbs[i].first.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
								totalW += crumbW[i];
								if (i > 0) totalW += gapW;
							}

							size_t visibleStart = 0;
							size_t visibleEnd   = crumbs.size();

							if (totalW > availW && crumbs.size() > 3)
							{
								float prefixW = crumbW[0] + gapW + ellipsisW + gapW;
								float suffixW = 0.0f;
								size_t tailCount = 0;

								for (size_t i = crumbs.size(); i > 1; --i)
								{
									float addW = crumbW[i - 1] + (tailCount > 0 ? gapW : 0.0f);
									if (prefixW + suffixW + addW <= availW)
									{
										suffixW += addW;
										tailCount++;
									}
									else break;
								}

								if (tailCount < 1) tailCount = 1;
								visibleEnd = crumbs.size() - tailCount;
							}

							bool collapsed = (visibleEnd < crumbs.size());
							size_t collapsedSkip = collapsed ? visibleEnd : 0;

							auto DrawCrumb = [&](size_t i) {
								if (i == lastIdx)
								{
									ImGui::PushStyleColor(ImGuiCol_Text, colHeaderText);
									ImGui::TextUnformatted(crumbs[i].first.c_str());
									ImGui::PopStyleColor();
								}
								else
								{
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
									ImGui::PushID(crumbs[i].first.c_str());
									if (ImGui::SmallButton(crumbs[i].first.c_str()))
									{
										m_CurrentPath = crumbs[i].second;
										NeedRefresh = true;
									}
									ImGui::PopID();
									ImGui::PopStyleColor();
								}
							};

							for (size_t i = visibleStart; i < visibleEnd; ++i)
							{
								if (i > visibleStart)
								{
									ImGui::SameLine(0, 2);
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
									ImGui::TextDisabled("/");
									ImGui::PopStyleColor();
									ImGui::SameLine(0, 2);
								}
								DrawCrumb(i);
							}

							if (collapsed)
							{
								ImGui::SameLine(0, 2);
								ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
								ImGui::TextDisabled("/");
								ImGui::PopStyleColor();
								ImGui::SameLine(0, 2);
								ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
								ImGui::TextDisabled("...");
								ImGui::PopStyleColor();

								for (size_t i = collapsedSkip; i < crumbs.size(); ++i)
								{
									ImGui::SameLine(0, 2);
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
									ImGui::TextDisabled("/");
									ImGui::PopStyleColor();
									ImGui::SameLine(0, 2);
									DrawCrumb(i);
								}
							}
						}
					}

					ImGui::PopID();
				}
				ImGui::TableSetColumnIndex(2);
				ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImColor(colBreadcrumb));
				ImGui::TextDisabled("  Preview");

				// ----------------------------------------------------------------------------------------------------------------
				//  ROW 2: source panel (left)  +  asset panel (right)
				// ----------------------------------------------------------------------------------------------------------------
				ImGui::TableNextRow(ImGuiTableRowFlags_None, 0);

				// --- LEFT: Source / folder tree (UE5 dark source panel) ---
				ImGui::TableSetColumnIndex(0);
				{
					// Push dark background for the entire source panel area
					ImGui::PushStyleColor(ImGuiCol_ChildBg, colSourcePanel);

					ImGuiTableFlags treeFlags =
						ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_ScrollY;

					if (ImGui::BeginTable("SourcePanelTree", 1, treeFlags, { 0, 0 }))
					{
						// ---- Render folder tree ----
						std::function<void(const std::filesystem::path&)> DrawFolderNode;
						DrawFolderNode = [&](const std::filesystem::path& dirPath)
						{
							std::vector<std::filesystem::path> subDirs;
							try
							{
								for (auto& entry : std::filesystem::directory_iterator(dirPath))
								{
									if (entry.is_directory())
										subDirs.push_back(entry.path());
								}
							}
							catch (...) { return; }

							std::sort(subDirs.begin(), subDirs.end());

							// Color for tree nodes
							ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(40, 40, 40, 255));
							ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(55, 55, 55, 255));
							ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32(50, 50, 50, 255));

							for (const auto& subPath : subDirs)
							{
								std::string folderName = subPath.filename().string();
								ImGui::TableNextRow();
								ImGui::TableNextColumn();

								bool isCurrent = (m_CurrentPath == subPath);
								bool isSelected = (m_SelectedPath == subPath);
								bool hasSubDirs = false;
								try
								{
									for (auto& sub : std::filesystem::directory_iterator(subPath))
									{
										if (sub.is_directory()) { hasSubDirs = true; break; }
									}
								}
								catch (...) {}

								ImGuiTreeNodeFlags nodeFlags =
									ImGuiTreeNodeFlags_SpanFullWidth |
									ImGuiTreeNodeFlags_OpenOnArrow |
									ImGuiTreeNodeFlags_OpenOnDoubleClick |
									ImGuiTreeNodeFlags_FramePadding;

								if (isSelected)
									nodeFlags |= ImGuiTreeNodeFlags_Selected;
								if (!hasSubDirs)
									nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

								// Draw folder tree node
								bool open = ImGui::TreeNodeEx(folderName.c_str(), nodeFlags);

								// Selection highlight - draw a blue left border for selected
								if (isCurrent || isSelected)
								{
									ImDrawList* draw = ImGui::GetWindowDrawList();
									ImVec2 itemMin = ImGui::GetItemRectMin();
									ImVec2 itemMax = ImGui::GetItemRectMax();
									draw->AddRectFilled(
										{ itemMin.x, itemMin.y },
										{ itemMin.x + 3.0f, itemMax.y },
										IM_COL32(60, 160, 255, 220));
								}

								if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
								{
									const bool arrowClick = ImGui::GetMousePos().x <
										ImGui::GetItemRectMin().x + ImGui::GetTreeNodeToLabelSpacing();
									if (!arrowClick)
										OnItemClicked(subPath, true, ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
								}

								if (open && hasSubDirs)
								{
									DrawFolderNode(subPath);
									ImGui::TreePop();
								}
							}

							ImGui::PopStyleColor(3);
						};

						std::filesystem::path treeRoot = KansFileSystem::GetAssetFolder().parent_path();
						DrawFolderNode(treeRoot);

						ImGui::EndTable();
					}

					ImGui::PopStyleColor(1);
				}

				// --- RIGHT: Asset grid panel ---
				ImGui::TableSetColumnIndex(1);
				{
					ImGui::PushStyleColor(ImGuiCol_ChildBg, colAssetPanel);

					// Asset area layout
					{
						// --- Toolbar row ( thin bar with filter/search placeholder) ---
						{
							ImGui::PushStyleColor(ImGuiCol_ChildBg, colBreadcrumb);
							ImGui::BeginChild("AssetToolbar", { 0, 26.0f }, false,
								ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
							{
								ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);

								// Filter text (muted placeholder)
								ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
								ImGui::TextUnformatted("  Filter Assets...");
								ImGui::PopStyleColor();

								// Right-aligned view options
								float lineH = ImGui::GetTextLineHeight();
								ImVec2 region = ImGui::GetContentRegionAvail();
								ImGui::SameLine(region.x - 80.0f);

								ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
								if (m_ComputedColumns > 1)
								{
									ImGui::TextDisabled("(%d cols)", m_ComputedColumns);
								}
								ImGui::PopStyleColor();
							}
							ImGui::EndChild();
							ImGui::PopStyleColor(1);
						}

							// ---- Asset grid container (child window adds margin) ----
						ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
						ImGui::BeginChild("AssetGridContainer", { 0, 0 }, false,
							ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
						{
						// --- Asset grid ---
						// ScrollY gives the table its own scrollbar; reserve that width
						// before deciding how many fixed-size cards can fit.
						const ImVec2 gridAvail = ImGui::GetContentRegionAvail();
						CalculateLayout(gridAvail.x - ImGui::GetStyle().ScrollbarSize,
							gridAvail.y, viewportSize.x, viewportSize.y);

						ImGuiTableFlags contentListFlag =
							ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoBordersInBody |
							ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX;

						// Push item card colors
						ImGui::PushStyleColor(ImGuiCol_Button,          IM_COL32(42, 42, 42, 255));
						ImGui::PushStyleColor(ImGuiCol_ButtonHovered,   IM_COL32(58, 58, 58, 255));
						ImGui::PushStyleColor(ImGuiCol_ButtonActive,    IM_COL32(52, 52, 52, 255));

						ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
						ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    { 6.0f, 6.0f });
						ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,     { 18.0f, 14.0f });
						ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   4.0f);
						ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,     { 8.0f, 4.0f });

						// ---- Refresh directory listing ----
						if (NeedRefresh)
						{
							NeedRefresh = false;
							m_ContentBrowserItemList.clear();

							for (auto& entry : std::filesystem::directory_iterator(m_CurrentPath))
							{
								std::string filename = entry.path().filename().string();
								AssetHandle handle = Hash::Generate64MD5Hash(filename);

								if (entry.is_directory())
								{
									m_ContentBrowserItemList.emplace_back(
										ContentBrowserItem::ItemType::Directory,
										handle, filename, EditorResources::FolderIcon,
										0, entry.last_write_time(), "");
								}
								else
								{
									// Source metadata and its atomic-write temporary files are internal assets.
									std::string normalizedName = filename;
									std::transform(normalizedName.begin(), normalizedName.end(), normalizedName.begin(),
										[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
									if ((normalizedName.size() >= 6 &&
										normalizedName.compare(normalizedName.size() - 6, 6, ".kmeta") == 0) ||
										normalizedName.find(".kmeta.tmp-") != std::string::npos)
										continue;

									std::string ext = entry.path().extension().string();
									m_ContentBrowserItemList.emplace_back(
										ContentBrowserItem::ItemType::Asset,
										handle, filename, EditorResources::GetFileIcon(entry.path()),
										entry.file_size(), entry.last_write_time(), ext);
								}
							}

							// Sort: directories first, then files; alphabetical within each group
							std::sort(m_ContentBrowserItemList.begin(), m_ContentBrowserItemList.end(),
								[](const ContentBrowserItem& a, const ContentBrowserItem& b) {
									if (a.m_Itemtype != b.m_Itemtype)
										return a.m_Itemtype == ContentBrowserItem::ItemType::Directory;
									return a.m_FileName < b.m_FileName;
								});
						}

						// ---- Render items in grid ----
						if (ImGui::BeginTable("AssetGrid", m_ComputedColumns, contentListFlag, { 0, 0 }))
						{
							for (auto& item : m_ContentBrowserItemList)
							{
								ImGui::TableNextColumn();
								item.OnRenderBegin();
								item.OnRender();
								item.OnRenderEnd();
							}

							ImGui::EndTable();
						}

							ImGui::PopStyleVar(5);
							ImGui::PopStyleColor(3); // 3 button colors
						}
						ImGui::EndChild();
						ImGui::PopStyleVar(1); // WindowPadding
					}

					ImGui::PopStyleColor(1); // ChildBg(colAssetPanel)
				}

				ImGui::TableSetColumnIndex(2);
				DrawPreview();
				ImGui::EndTable();
			}
		}

		ImGui::PopStyleColor(1); // TableBorderStrong
		ImGui::PopStyleVar(1); // FrameBorderSize
		ImGui::End();
	}

}
