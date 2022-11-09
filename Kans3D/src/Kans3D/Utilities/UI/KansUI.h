#pragma once
#include <glm/glm.hpp>
#include "Kans3D/Scene/Entity.h"
#include <imgui.h>
#include <imgui_internal.h>
namespace Kans::UI
{

	void DrawVec4Control(const std::string& label, glm::vec4& value, float resetvalue = 0.0f, float columwidth = 100.0f);
	void DrawVec3Control(const std::string& label, glm::vec3& value, float resetvalue = 0.0f, float columwidth = 100.0f);
	void DrawVec2Control(const std::string& label, glm::vec2& value, float resetvalue = 0.0f, float columwidth = 100.0f);
	void DrawFloatControl(const std::string& label, float& value, float resetvalue = 0.0f, float columwidth = 100.0f);
	void DrawIntControl(const std::string& label, int& value, int resetvalue = 0.0f, float columwidth = 100.0f);

	template <typename T, typename UIfunc>
	void DrawComponent(const std::string& label, Entity entity, UIfunc uifunc)
		 {
			 const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed
				 | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
			 if (entity.HasComponent<T>())
			 {
				 ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));

				 auto contentregion = ImGui::GetContentRegionAvail();
				 auto& component = entity.GetComponent<T>();
				 float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
				
				 
					
				 //component title
				 bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, label.c_str());
				 ImGui::PopStyleVar();
				 //Remove Button
				 ImGui::SameLine(contentregion.x - lineHeight);
				 if (ImGui::Button("-", ImVec2(lineHeight, lineHeight)))
				 {
					 ImGui::OpenPopup("ComponentSetting");
				 }

				 bool RemovedComponent = false;
				 if (ImGui::BeginPopup("ComponentSetting"))
				 {
					 if (ImGui::MenuItem("Remove Component"))
					 {
						 RemovedComponent = true;
					 }
					 ImGui::EndPopup();
				 }
				 if (open)
				 {
					 uifunc(component);
					 ImGui::TreePop();
				 }
				 if (RemovedComponent)
				 {
					 entity.RemoveComponent<T>();
				 }
			 }


		};
}
