#pragma once

#include "Kans3D/Editor/EditorPanel.h"
#include "Kans3D/FileSystem/FileSystem.h"
#include "Kans3D/Editor/EditorResources.h"
#include "ContentBrowserItem.h"

#include <vector>

namespace Kans
{
	class ContentBrowserPanel : public EditorPanel
	{
	public:
		// ©¤©¤ View modes: progressively richer layout as icon tier grows ©¤©¤
		enum class ViewMode : uint8_t
		{
			CompactGrid,   // 48 / 64 px   ¡ú  icon + truncated name
			StandardGrid,  // 80 / 96 px   ¡ú  icon + name + extension badge
			DetailedGrid,  // 128 px       ¡ú  icon + name + ext + size + date
			ExpandedGrid   // 160 / 192 px ¡ú  icon + name + ext + size + full date
		};

	public:
		ContentBrowserPanel();
		virtual void onImGuiRender(bool isOpen) override;

		static ContentBrowserPanel* Get() { return s_Instance; }

		// ©¤©¤ Queries (called by ContentBrowserItem) ©¤©¤
		float    GetCurrentIconSize() const { return m_CurrentIconSize; }
		ViewMode GetCurrentViewMode() const { return m_CurrentViewMode; }

	private:
		// Snaps icon size to the best discrete tier based on available panel width.
		void CalculateLayout(float availableWidth);

		// ©¤©¤ Discrete icon-size tiers (snap-to sizes, not smooth interpolation) ©¤©¤
		static constexpr float kIconTiers[]    = { 48.0f, 64.0f, 80.0f, 96.0f, 128.0f  };
		static constexpr int   kNumIconTiers   = 5;
		static constexpr int   kMinColumns     = 2;
		static constexpr int   kMaxColumns     = 16;

		std::vector<ContentBrowserItem> m_ContentBrowserItemList;

		std::filesystem::path m_CurrentPath;
		int open_action = -1;

		// Layout / styling
		float FramePadding    = 8.0f;
		float FrameBorderSize = 1.5f;
		float FrameRounding   = 8.0f;

		// ©¤©¤ Auto-scaling state ©¤©¤
		float    m_CurrentIconSize   = 96.0f;
		int      m_ComputedColumns   = 4;          // derived from layout calc
		float    m_LastAvailableWidth = 0.0f;
		ViewMode m_CurrentViewMode   = ViewMode::StandardGrid;

		glm::vec2 OuterSize        = { 0, 0 };
		glm::vec2 ItemInnerSpacing = { 36, 15 };

		bool NeedRefresh = true;
		static ContentBrowserPanel* s_Instance;
		friend class ContentBrowserItem;
	};
}
