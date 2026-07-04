#include "kspch.h"
#include "ContentBrowserItem.h"
#include "ContentBrowserPanel.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace Kans
{
	// ── tiny local helpers ──────────────────────────────────────────────

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

	static std::string FormatDate(const std::filesystem::file_time_type& ftime)
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

	// ── Truncate text to fit pixel width, append "..." if clipped ──
	static void TextEllipsis(const char* text, float maxWidth)
	{
		const float ellipsisW = ImGui::CalcTextSize("...").x;
		float fullW = ImGui::CalcTextSize(text).x;
		if (fullW <= maxWidth)
		{
			ImGui::TextUnformatted(text);
			return;
		}

		int lo = 0, hi = static_cast<int>(strlen(text));
		while (lo < hi)
		{
			int mid = (lo + hi + 1) / 2;
			std::string sub(text, mid);
			float w = ImGui::CalcTextSize(sub.c_str()).x + ellipsisW;
			if (w <= maxWidth)
				lo = mid;
			else
				hi = mid - 1;
		}
		std::string result(text, lo);
		result += "...";
		ImGui::TextUnformatted(result.c_str());
	}

	// ── Constructor ────────────────────────────────────────────────────

	ContentBrowserItem::ContentBrowserItem(ItemType type, AssetHandle handle,
		std::string name, const Ref<Texture2D>& icon,
		uint64_t fileSize, std::filesystem::file_time_type lastWriteTime,
		std::string extension)
		: m_Itemtype(type), m_AssetHandle(handle), m_FileName(std::move(name)),
		m_Icon(icon), m_FileSize(fileSize), m_FileExtension(std::move(extension)),
		m_ModifiedDateStr(FormatDate(lastWriteTime))
	{
	}

	// ── Render entry points ────────────────────────────────────────────

	void ContentBrowserItem::OnRenderBegin()
	{
		ImGui::PushID(&m_AssetHandle);
	}

	void ContentBrowserItem::OnRender()
	{
		ImGui::BeginGroup();

		ImTextureID texID = (ImTextureID)m_Icon->GetRenererID();
		float iconSize = ContentBrowserPanel::Get()->GetCurrentIconSize();
		ImVec2 size(iconSize, iconSize);

		// ── dispatch to view-mode renderer ──
		using VM = ContentBrowserPanel::ViewMode;
		switch (ContentBrowserPanel::Get()->GetCurrentViewMode())
		{
		case VM::CompactGrid:   RenderCompactGrid(size, texID);   break;
		case VM::StandardGrid:  RenderStandardGrid(size, texID);  break;
		case VM::DetailedGrid:  RenderDetailedGrid(size, texID);  break;
		case VM::ExpandedGrid:  RenderExpandedGrid(size, texID);  break;
		}

		ImGui::EndGroup();

		// ── drag-drop source (shared) ──
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			ImGui::Image(texID, ImVec2(20, 20), { 0, 1 }, { 1, 0 });
			ImGui::SameLine();
			ImGui::TextUnformatted(m_FileName.c_str());

			m_path = (ContentBrowserPanel::Get()->m_CurrentPath / m_FileName).generic_string();
			ImGui::SetDragDropPayload("asset_payload", m_path.data(), m_path.size());
			ImGui::EndDragDropSource();
		}
	}

	void ContentBrowserItem::OnRenderEnd()
	{
		ImGui::PopID();
	}

	// ────────────────────────────────────────────────────────────────────
	//  card rendering helpers
	// ────────────────────────────────────────────────────────────────────

	// Draw the card background and hover highlight, returns true if clicked
	static bool DrawAssetCard(const ImVec2& cardMin, const ImVec2& cardMax,
		float rounding, bool isDirectory, bool isHovered)
	{
		ImDrawList* draw = ImGui::GetWindowDrawList();

		// Card background
		ImU32 bgColor = IM_COL32(42, 42, 42, 255);
		if (isHovered)
			bgColor = isDirectory ? IM_COL32(55, 55, 55, 255) : IM_COL32(58, 58, 58, 255);

		// Draw card background with rounded corners
		draw->AddRectFilled(cardMin, cardMax, bgColor, rounding);

		// Subtle border
		draw->AddRect(cardMin, cardMax, IM_COL32(55, 55, 55, 180), rounding, 0, 1.0f);

		// Bottom accent line for directories 
		if (isDirectory)
		{
			draw->AddRectFilled(
				{ cardMin.x + 2.0f, cardMax.y - 2.0f },
				{ cardMax.x - 2.0f, cardMax.y - 1.0f },
				IM_COL32(60, 160, 255, 120), 1.0f);
		}

		// Hover glow
		if (isHovered)
		{
			draw->AddRect(cardMin, cardMax, IM_COL32(80, 80, 80, 255), rounding, 0, 1.5f);
		}

		return false;
	}

	// ── CompactGrid: small card with icon + truncated name ─────────────
	void ContentBrowserItem::RenderCompactGrid(const ImVec2& iconSize, ImTextureID texID)
	{
		ImGuiContext& g = *GImGui;
		ImDrawList* draw = ImGui::GetWindowDrawList();

		float cardW = iconSize.x + 12.0f;
		float cardH = iconSize.y + 32.0f;
		float rounding = 4.0f;

		ImVec2 cursorPos = ImGui::GetCursorScreenPos();
		ImVec2 cardMin(cursorPos.x, cursorPos.y);
		ImVec2 cardMax(cursorPos.x + cardW, cursorPos.y + cardH);

		// Reserve space
		ImGui::Dummy({ cardW, cardH });

		bool isHovered = ImGui::IsMouseHoveringRect(cardMin, cardMax, false);

		// Draw card background
		DrawAssetCard(cardMin, cardMax, rounding, m_Itemtype == ItemType::Directory, isHovered);

		// Center icon in the card with padding
		float padX = (cardW - iconSize.x) * 0.5f;
		float padY = 6.0f;

		ImVec2 iconMin(cardMin.x + padX, cardMin.y + padY);
		ImVec2 iconMax(iconMin.x + iconSize.x, iconMin.y + iconSize.y);

		draw->AddImage(texID, iconMin, iconMax, { 0, 1 }, { 1, 0 });

		// Name below icon
		float nameY = iconMax.y + 4.0f;
		float nameMaxW = cardW - 8.0f;
		ImGui::SetCursorScreenPos({ cardMin.x + 4.0f, nameY });

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
		TextEllipsis(m_FileName.c_str(), nameMaxW);
		ImGui::PopStyleColor();

		// Click handling
		ImGui::SetCursorScreenPos(cardMin);
		ImGui::InvisibleButton("click", { cardW, cardH });
		if (ImGui::IsItemActivated() && m_Itemtype == ItemType::Directory)
		{
			auto* panel = ContentBrowserPanel::Get();
			panel->m_CurrentPath /= m_FileName;
			panel->NeedRefresh = true;
		}
	}

	// ── StandardGrid: card with icon + name + extension badge ──────────
	void ContentBrowserItem::RenderStandardGrid(const ImVec2& iconSize, ImTextureID texID)
	{
		ImDrawList* draw = ImGui::GetWindowDrawList();

		float cardW = iconSize.x + 16.0f;
		float cardH = iconSize.y + 50.0f;
		float rounding = 5.0f;

		ImVec2 cursorPos = ImGui::GetCursorScreenPos();
		ImVec2 cardMin(cursorPos.x, cursorPos.y);
		ImVec2 cardMax(cursorPos.x + cardW, cursorPos.y + cardH);

		ImGui::Dummy({ cardW, cardH });

		bool isHovered = ImGui::IsMouseHoveringRect(cardMin, cardMax, false);
		DrawAssetCard(cardMin, cardMax, rounding, m_Itemtype == ItemType::Directory, isHovered);

		// Center icon
		float padX = (cardW - iconSize.x) * 0.5f;
		float padY = 8.0f;
		ImVec2 iconMin(cardMin.x + padX, cardMin.y + padY);
		ImVec2 iconMax(iconMin.x + iconSize.x, iconMin.y + iconSize.y);
		draw->AddImage(texID, iconMin, iconMax, { 0, 1 }, { 1, 0 });

		// Name
		float nameY = iconMax.y + 5.0f;
		float nameMaxW = cardW - 8.0f;
		ImGui::SetCursorScreenPos({ cardMin.x + 4.0f, nameY });

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
		TextEllipsis(m_FileName.c_str(), nameMaxW);
		ImGui::PopStyleColor();

		// Extension badge (only for assets)
		if (m_Itemtype == ItemType::Asset && !m_FileExtension.empty())
		{
			ImGui::SetCursorScreenPos({ cardMin.x + 4.0f, nameY + ImGui::GetTextLineHeight() + 1.0f });
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
			TextEllipsis(m_FileExtension.c_str(), nameMaxW);
			ImGui::PopStyleColor();
		}

		// Click handling
		ImGui::SetCursorScreenPos(cardMin);
		ImGui::InvisibleButton("click", { cardW, cardH });
		if (ImGui::IsItemActivated() && m_Itemtype == ItemType::Directory)
		{
			auto* panel = ContentBrowserPanel::Get();
			panel->m_CurrentPath /= m_FileName;
			panel->NeedRefresh = true;
		}
	}

	// ── DetailedGrid: card with icon + name + ext + size + date ────────
	void ContentBrowserItem::RenderDetailedGrid(const ImVec2& iconSize, ImTextureID texID)
	{
		ImDrawList* draw = ImGui::GetWindowDrawList();

		float cardW = iconSize.x + 20.0f;
		float cardH = iconSize.y + 66.0f;
		float rounding = 5.0f;

		ImVec2 cursorPos = ImGui::GetCursorScreenPos();
		ImVec2 cardMin(cursorPos.x, cursorPos.y);
		ImVec2 cardMax(cursorPos.x + cardW, cursorPos.y + cardH);

		ImGui::Dummy({ cardW, cardH });

		bool isHovered = ImGui::IsMouseHoveringRect(cardMin, cardMax, false);
		DrawAssetCard(cardMin, cardMax, rounding, m_Itemtype == ItemType::Directory, isHovered);

		// Icon
		float padX = (cardW - iconSize.x) * 0.5f;
		float padY = 10.0f;
		ImVec2 iconMin(cardMin.x + padX, cardMin.y + padY);
		ImVec2 iconMax(iconMin.x + iconSize.x, iconMin.y + iconSize.y);
		draw->AddImage(texID, iconMin, iconMax, { 0, 1 }, { 1, 0 });

		float nameMaxW = cardW - 8.0f;
		float textY = iconMax.y + 5.0f;
		ImGui::SetCursorScreenPos({ cardMin.x + 4.0f, textY });

		// Name
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
		TextEllipsis(m_FileName.c_str(), nameMaxW);
		ImGui::PopStyleColor();

		float lineH = ImGui::GetTextLineHeight();

		// Extension + size
		if (m_Itemtype == ItemType::Asset)
		{
			ImGui::SetCursorScreenPos({ cardMin.x + 4.0f, textY + lineH + 2.0f });
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
			std::string info = m_FileExtension;
			if (m_FileSize > 0)
				info += "  " + FormatFileSize(m_FileSize);
			TextEllipsis(info.c_str(), nameMaxW);
			ImGui::PopStyleColor();
		}

		// Date
		{
			ImGui::SetCursorScreenPos({ cardMin.x + 4.0f, textY + lineH * 2.0f + 3.0f });
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
			TextEllipsis(m_ModifiedDateStr.c_str(), nameMaxW);
			ImGui::PopStyleColor();
		}

		// Click
		ImGui::SetCursorScreenPos(cardMin);
		ImGui::InvisibleButton("click", { cardW, cardH });
		if (ImGui::IsItemActivated() && m_Itemtype == ItemType::Directory)
		{
			auto* panel = ContentBrowserPanel::Get();
			panel->m_CurrentPath /= m_FileName;
			panel->NeedRefresh = true;
		}
	}

	// ── ExpandedGrid: full card with all metadata ──────────────────────
	void ContentBrowserItem::RenderExpandedGrid(const ImVec2& iconSize, ImTextureID texID)
	{
		ImDrawList* draw = ImGui::GetWindowDrawList();

		float cardW = iconSize.x + 24.0f;
		float cardH = iconSize.y + 80.0f;
		float rounding = 6.0f;

		ImVec2 cursorPos = ImGui::GetCursorScreenPos();
		ImVec2 cardMin(cursorPos.x, cursorPos.y);
		ImVec2 cardMax(cursorPos.x + cardW, cursorPos.y + cardH);

		ImGui::Dummy({ cardW, cardH });

		bool isHovered = ImGui::IsMouseHoveringRect(cardMin, cardMax, false);
		DrawAssetCard(cardMin, cardMax, rounding, m_Itemtype == ItemType::Directory, isHovered);

		// Icon
		float padX = (cardW - iconSize.x) * 0.5f;
		float padY = 12.0f;
		ImVec2 iconMin(cardMin.x + padX, cardMin.y + padY);
		ImVec2 iconMax(iconMin.x + iconSize.x, iconMin.y + iconSize.y);
		draw->AddImage(texID, iconMin, iconMax, { 0, 1 }, { 1, 0 });

		float nameMaxW = cardW - 10.0f;
		float textY = iconMax.y + 6.0f;

		// Name
		ImGui::SetCursorScreenPos({ cardMin.x + 5.0f, textY });
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
		TextEllipsis(m_FileName.c_str(), nameMaxW);
		ImGui::PopStyleColor();

		float lineH = ImGui::GetTextLineHeight();

		// Extension badge
		if (m_Itemtype == ItemType::Asset && !m_FileExtension.empty())
		{
			ImGui::SetCursorScreenPos({ cardMin.x + 5.0f, textY + lineH + 2.0f });
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.75f, 1.0f));
			TextEllipsis(m_FileExtension.c_str(), nameMaxW);
			ImGui::PopStyleColor();
		}

		// File size
		if (m_Itemtype == ItemType::Asset && m_FileSize > 0)
		{
			float sizeY = textY + lineH * (m_FileExtension.empty() ? 1 : 2) + 2.0f;
			ImGui::SetCursorScreenPos({ cardMin.x + 5.0f, sizeY });
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
			TextEllipsis(FormatFileSize(m_FileSize).c_str(), nameMaxW);
			ImGui::PopStyleColor();
		}

		// Date
		{
			int infoLines = 1; // name
			if (m_Itemtype == ItemType::Asset && !m_FileExtension.empty()) infoLines++;
			if (m_Itemtype == ItemType::Asset && m_FileSize > 0) infoLines++;

			float dateY = textY + lineH * infoLines + 3.0f;
			ImGui::SetCursorScreenPos({ cardMin.x + 5.0f, dateY });
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.35f, 0.35f, 1.0f));
			TextEllipsis(m_ModifiedDateStr.c_str(), nameMaxW);
			ImGui::PopStyleColor();
		}

		// Click
		ImGui::SetCursorScreenPos(cardMin);
		ImGui::InvisibleButton("click", { cardW, cardH });
		if (ImGui::IsItemActivated() && m_Itemtype == ItemType::Directory)
		{
			auto* panel = ContentBrowserPanel::Get();
			panel->m_CurrentPath /= m_FileName;
			panel->NeedRefresh = true;
		}
	}

}
