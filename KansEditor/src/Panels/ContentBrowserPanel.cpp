#include "ContentBrowserPanel.h"
#include "Kans3D/ImGui/KansUI.h"
#include "Kans3D/ImGui/Colors.h"
#include "Kans3D/Core/Hash.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <sstream>

#define ADJUST_CONTRNTBROWSER false

namespace Kans
{
	ContentBrowserPanel* ContentBrowserPanel::s_Instance = nullptr;

	// ── tiny helpers (file-size / date formatting for item metadata) ──
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
		std::tm tm_buf{};
		localtime_s(&tm_buf, &tt);
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

	// ------------------------------------------------------------------
	//  CalculateLayout  –  snap to discrete icon-size tier based on
	//  the *panel content area* width (not total window width).
	// ------------------------------------------------------------------
	void ContentBrowserPanel::CalculateLayout(float availableWidth)
	{
		if (availableWidth <= 0.0f)
		{
			m_CurrentIconSize = kIconTiers[0];
			m_ComputedColumns = 1;
			m_CurrentViewMode = ViewMode::CompactGrid;
			return;
		}

		// Walk tiers largest → smallest; pick the largest that fits ≥ kMinColumns.
		for (int i = kNumIconTiers - 1; i >= 0; --i)
		{
			float candidate = kIconTiers[i];
			float perCol    = candidate + FramePadding * 2.0f + ItemInnerSpacing.x;
			int   cols      = static_cast<int>(availableWidth / perCol);

			if (cols >= kMinColumns)
			{
				m_CurrentIconSize = candidate;
				m_ComputedColumns = std::min(cols, kMaxColumns);
				break;
			}

			// Last resort – smallest tier, at least 1 column
			if (i == 0)
			{
				m_CurrentIconSize = candidate;
				m_ComputedColumns = std::max(static_cast<int>(availableWidth / perCol), 1);
			}
		}

		// ── Map discrete tier → view mode ──
		if      (m_CurrentIconSize <= 64.0f)  m_CurrentViewMode = ViewMode::CompactGrid;
		else if (m_CurrentIconSize <= 96.0f)  m_CurrentViewMode = ViewMode::StandardGrid;
		else if (m_CurrentIconSize <= 128.0f) m_CurrentViewMode = ViewMode::DetailedGrid;
		else                                  m_CurrentViewMode = ViewMode::ExpandedGrid;
	}

	// ── Draw a thin horizontal separator line ──
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
		auto& style = ImGui::GetStyle();

		ImGui::Begin("ContentBrowser", nullptr, windowFlags);

		// ── Background colors for  panels ──
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

		ImVec2 display_size = ImGui::GetIO().DisplaySize;
		style.WindowMinSize.x = 600;
		style.WindowMinSize.y = display_size.y * 0.20f;

		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

		// ── Outer split table: left = source panel, right = asset panel ──
		{
			static ImGuiTableFlags flags =
				ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV |
				ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoPadOuterX |
				ImGuiTableFlags_SizingFixedSame;

			ImVec2 tableSize = ImGui::GetWindowSize();
			tableSize.y *= 0.94f;

			// Subtle  vertical separator
		ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, ImVec4(0.18f, 0.18f, 0.18f, 1.0f));
		if (ImGui::BeginTable("ContentBrowserSplit", 2, flags, { 0, tableSize.y }))
			{
				// ── Column setup with widths ──
				ImGui::TableSetupColumn(
					KansFileSystem::GetAssetFolder().filename().string().c_str(),
					ImGuiTableColumnFlags_NoHeaderLabel | ImGuiTableColumnFlags_NoHide /*| ImGuiTableColumnFlags_WidthFixed*/,
					220.0f);   // default source panel width like UE5
				ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoHide /*| ImGuiTableColumnFlags_WidthStretch*/);

				// ────────────────────────────────────────────────────────
				//  ROW 1: column headers (breadcrumb row)
				// ────────────────────────────────────────────────────────
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

				// ────────────────────────────────────────────────────────
				//  ROW 2: source panel (left)  +  asset panel (right)
				// ────────────────────────────────────────────────────────
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
						// ── Render folder tree ──
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

								if (isCurrent)
									nodeFlags |= ImGuiTreeNodeFlags_Selected;
								if (!hasSubDirs)
									nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

								// Draw folder tree node
								bool open = ImGui::TreeNodeEx(folderName.c_str(), nodeFlags);

								// Selection highlight - draw a blue left border for selected
								if (isCurrent)
								{
									ImDrawList* draw = ImGui::GetWindowDrawList();
									ImVec2 itemMin = ImGui::GetItemRectMin();
									ImVec2 itemMax = ImGui::GetItemRectMax();
									draw->AddRectFilled(
										{ itemMin.x, itemMin.y },
										{ itemMin.x + 3.0f, itemMax.y },
										IM_COL32(60, 160, 255, 220));
								}

								if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
								{
									m_CurrentPath = subPath;
									NeedRefresh = true;
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

							// ── Asset grid container (child window adds margin) ──
						ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
						ImGui::BeginChild("AssetGridContainer", { 0, 0 }, false,
							ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
						{
						// --- Asset grid ---
						float panelAvailW = ImGui::GetContentRegionAvail().x;
						if (std::abs(panelAvailW - m_LastAvailableWidth) > 1.0f)
						{
							m_LastAvailableWidth = panelAvailW;
							CalculateLayout(panelAvailW);
						}

						ItemInnerSpacing.y = m_CurrentIconSize * 0.25f;

						ImGuiTableFlags contentListFlag =
							ImGuiTableFlags_ScrollY | ImGuiTableFlags_NoBordersInBody |
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

						// ── Refresh directory listing ──
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
									std::string ext = entry.path().extension().string();
									m_ContentBrowserItemList.emplace_back(
										ContentBrowserItem::ItemType::Asset,
										handle, filename, EditorResources::FbxFileIcon,
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

						// ── Render items in grid ──
						if (ImGui::BeginTable("AssetGrid", m_ComputedColumns, contentListFlag, { 0, 0 }))
						{
							ImGui::TableNextColumn();

							for (auto& item : m_ContentBrowserItemList)
							{
								item.OnRenderBegin();
								item.OnRender();
								item.OnRenderEnd();
								ImGui::TableNextColumn();
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

				ImGui::EndTable();
			}
		}

		ImGui::PopStyleColor(1); // TableBorderStrong
		ImGui::PopStyleVar(1); // FrameBorderSize
		ImGui::End();
	}

}
