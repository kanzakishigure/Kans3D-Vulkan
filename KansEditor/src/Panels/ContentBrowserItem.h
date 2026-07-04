#pragma once

#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Renderer/Resource/Texture.h"

#include "imgui/imgui.h"
#include <string>
#include <cstdint>
#include <filesystem>

namespace Kans
{
	class ContentBrowserItem
	{
	public:
		enum class ItemType : uint16_t
		{
			Directory, Asset
		};

	public:
		ContentBrowserItem(ItemType type, AssetHandle handle, std::string name,
			const Ref<Texture2D>& icon,
			uint64_t fileSize = 0,
			std::filesystem::file_time_type lastWriteTime = {},
			std::string extension = "");

		void OnRenderBegin();
		void OnRender();
		void OnRenderEnd();

	private:
		// ── Metadata ──
		ItemType                           m_Itemtype;
		AssetHandle                        m_AssetHandle;
		std::string                        m_FileName;
		Ref<Texture2D>                     m_Icon;

		// ── File details for progressive display ──
		uint64_t                           m_FileSize = 0;
		std::string                        m_FileExtension;       // e.g. ".png"
		std::string                        m_ModifiedDateStr;     // pre-formatted  "2024-01-15"

		// ── Rendering helpers per view mode ──
		void RenderCompactGrid(const ImVec2& iconSize, ImTextureID texID);
		void RenderStandardGrid(const ImVec2& iconSize, ImTextureID texID);
		void RenderDetailedGrid(const ImVec2& iconSize, ImTextureID texID);
		void RenderExpandedGrid(const ImVec2& iconSize, ImTextureID texID);

		// drag-drop
		std::string m_path;

		friend class ContentBrowserPanel;
	};
}
