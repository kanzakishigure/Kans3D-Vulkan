#pragma once
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Renderer/Resource/Texture.h"

#include "imgui/imgui.h"
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
		ContentBrowserItem(ItemType type,AssetHandle handle ,std::string name ,const Ref<Texture2D>& icon);

		void OnRenderBegin();
		void OnRender();
		void OnRenderEnd();
	private:
		ItemType m_Itemtype;
		AssetHandle m_AssetHandle;
		std::string m_FileName;
		Ref<Texture2D> m_Icon;

		ImVec2 m_ImageButtonSize = { 96, 96};

		//temp
		std::string m_path;
		friend class ContentBrowserPanel;
	};
}