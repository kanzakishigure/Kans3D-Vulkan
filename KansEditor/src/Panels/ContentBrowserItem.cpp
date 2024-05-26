#include "kspch.h"
#include "ContentBrowserItem.h"
#include "ContentBrowserPanel.h"


#include <imgui_internal.h>

namespace Kans
{

	

	ContentBrowserItem::ContentBrowserItem(ItemType type,AssetHandle handle, std::string name, const Ref<Texture2D>& icon)
		:m_Itemtype(type), m_AssetHandle(handle),m_FileName(name),m_Icon(icon)
	{

	}

	void ContentBrowserItem::OnRenderBegin()
	{
		ImGui::PushID(&m_AssetHandle);
		
	}

	
	void ContentBrowserItem::OnRender()
	{
		ImGui::BeginGroup();
		ImTextureID IconID = (ImTextureID)m_Icon->GetRenererID();
		
		ImGuiContext& g = *GImGui;
		if (ImGui::ImageButtonEx(m_AssetHandle,IconID, m_ImageButtonSize, { 0,1 }, { 1,0 }, g.Style.FramePadding, ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1)))
		{
			if (m_Itemtype == ItemType::Directory)
			{
				ContentBrowserPanel::Get()->m_CurrentPath /= m_FileName;
				ContentBrowserPanel::Get()->NeedRefresh = true;
			}
		}
		ImGui::Text("%s", m_FileName.c_str());
		ImGui::EndGroup();
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
		{
			ImGui::Image(IconID, ImVec2(20, 20), { 0,1 }, { 1,0 });
			ImGui::SameLine();
			ImGui::TextUnformatted(m_FileName.c_str());


			// Set payload to carry the index of our item (could be anything)

			m_path = (ContentBrowserPanel::Get()->m_CurrentPath / m_FileName).generic_string();
			
			ImGui::SetDragDropPayload("asset_payload", m_path.data(), m_path.size());
			ImGui::EndDragDropSource();
		}
		
		
		
	}

	void ContentBrowserItem::OnRenderEnd()
	{
		
		ImGui::PopID();
		//ImGui::NextColumn();
	}

}