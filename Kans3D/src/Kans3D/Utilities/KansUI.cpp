#include "hzpch.h"
#include "KansUI.h"

namespace Kans
{

	void KansUI::DrawVec4Control(const std::string& label, glm::vec4& value, float resetvalue /*= 0.0f*/, float columwidth /*= 100.0f*/)
	{
		auto boldFont = ImGui::GetIO().Fonts->Fonts[1];
		//label
		ImGui::PushID(label.c_str());
		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, columwidth);
		ImGui::Text(label.c_str());
		ImGui::NextColumn();

		//value
		ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
		ImVec2 buttonsize = { lineHeight + 3.0f,lineHeight };
		//X
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(221 / 256.0f, 15 / 256.0f, 116 / 256.0f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(221 / 256.0f, 15 / 256.0f, 116 / 256.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(197 / 256.0f, 19 / 256.0f, 104 / 256.0f, 1.0f));

			ImGui::PushFont(boldFont);
			if (ImGui::Button("X", buttonsize))
				value.x = resetvalue;
			ImGui::PopFont();

			ImGui::SameLine();
			ImGui::DragFloat("##X", &value.x, 0.1f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::PopStyleColor(3);
		}
		//Y
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(43 / 256.0f, 159 / 256.0f, 15 / 256.0f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(43 / 256.0f, 159 / 256.0f, 15 / 256.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(39 / 256.0f, 108 / 256.0f, 10 / 256.0f, 1.0f));

			ImGui::PushFont(boldFont);
			if (ImGui::Button("Y", buttonsize))
				value.y = resetvalue;
			ImGui::PopFont();

			ImGui::SameLine();

			ImGui::DragFloat("##Y", &value.y, 0.1f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::PopStyleColor(3);
		}

		//Z
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(18 / 256.0f, 186 / 256.0f, 190 / 256.0f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(18 / 256.0f, 186 / 256.0f, 190 / 256.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(22 / 256.0f, 229 / 256.0f, 228 / 256.0f, 1.0f));

			ImGui::PushFont(boldFont);
			if (ImGui::Button("Z", buttonsize))
				value.z = resetvalue;
			ImGui::PopFont();

			ImGui::SameLine();
			ImGui::DragFloat("##Z", &value.z, 0.1f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			ImGui::PopStyleColor(3);
		}
		//W
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(18 / 256.0f, 186 / 256.0f, 190 / 256.0f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(18 / 256.0f, 186 / 256.0f, 190 / 256.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(22 / 256.0f, 229 / 256.0f, 228 / 256.0f, 1.0f));

			ImGui::PushFont(boldFont);
			if (ImGui::Button("W", buttonsize))
				value.w = resetvalue;
			ImGui::PopFont();

			ImGui::SameLine();
			ImGui::DragFloat("##W", &value.w, 0.1f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			ImGui::PopStyleColor(3);
		}

		ImGui::PopStyleVar();
		ImGui::Columns(1);
		ImGui::PopID();
	}

	void KansUI::DrawVec3Control(const std::string& label, glm::vec3& value, float resetvalue /*= 0.0f*/, float columwidth /*= 100.0f*/)
	{

		auto boldFont = ImGui::GetIO().Fonts->Fonts[1];
		//label
		ImGui::PushID(label.c_str());
		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, columwidth);
		ImGui::Text(label.c_str());
		ImGui::NextColumn();

		//value
		ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
		ImVec2 buttonsize = { lineHeight + 3.0f,lineHeight };
		//X
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(221 / 256.0f, 15 / 256.0f, 116 / 256.0f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(221 / 256.0f, 15 / 256.0f, 116 / 256.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(197 / 256.0f, 19 / 256.0f, 104 / 256.0f, 1.0f));
			
			ImGui::PushFont(boldFont);
			if (ImGui::Button("X", buttonsize))
				value.x = resetvalue;
			ImGui::PopFont();

			ImGui::SameLine();
			ImGui::DragFloat("##X", &value.x, 0.1f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::PopStyleColor(3);
		}
		//Y
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(43 /256.0f, 159 /256.0f, 15 /256.0f,0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(43 / 256.0f, 159 / 256.0f, 15 / 256.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(39 / 256.0f, 108 / 256.0f, 10 / 256.0f, 1.0f));
			
			ImGui::PushFont(boldFont);
			if (ImGui::Button("Y", buttonsize))
				value.y = resetvalue;
			ImGui::PopFont();

			ImGui::SameLine();
			
			ImGui::DragFloat("##Y", &value.y, 0.1f,0.0f,0.0f,"%.2f");
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::PopStyleColor(3);
		}
		
		//Z
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(18 / 256.0f, 186 / 256.0f, 190 / 256.0f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(18 / 256.0f, 186 / 256.0f, 190 / 256.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(22 / 256.0f, 229 / 256.0f, 228 / 256.0f, 1.0f));
			
			ImGui::PushFont(boldFont);
			if (ImGui::Button("Z", buttonsize))
				value.z = resetvalue;
			ImGui::PopFont();

			ImGui::SameLine();
			ImGui::DragFloat("##Z", &value.z, 0.1f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			ImGui::PopStyleColor(3);
		}
		

		ImGui::PopStyleVar();
		ImGui::Columns(1);
		ImGui::PopID();
	}

	void KansUI::DrawVec2Control(const std::string& label, glm::vec2& value, float resetvalue /*= 0.0f*/, float columwidth /*= 100.0f*/)
	{
		auto boldFont = ImGui::GetIO().Fonts->Fonts[1];
		//label
		ImGui::PushID(label.c_str());
		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, columwidth);
		ImGui::Text(label.c_str());
		ImGui::NextColumn();

		//value
		ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
		ImVec2 buttonsize = { lineHeight + 3.0f,lineHeight };
		//X
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(221 / 256.0f, 15 / 256.0f, 116 / 256.0f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(221 / 256.0f, 15 / 256.0f, 116 / 256.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(197 / 256.0f, 19 / 256.0f, 104 / 256.0f, 1.0f));

			ImGui::PushFont(boldFont);
			if (ImGui::Button("X", buttonsize))
				value.x = resetvalue;
			ImGui::PopFont();

			ImGui::SameLine();
			ImGui::DragFloat("##X", &value.x, 0.005f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::PopStyleColor(3);
		}
		//Y
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(43 / 256.0f, 159 / 256.0f, 15 / 256.0f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(43 / 256.0f, 159 / 256.0f, 15 / 256.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(39 / 256.0f, 108 / 256.0f, 10 / 256.0f, 1.0f));

			ImGui::PushFont(boldFont);
			if (ImGui::Button("Y", buttonsize))
				value.y = resetvalue;
			ImGui::PopFont();

			ImGui::SameLine();

			ImGui::DragFloat("##Y", &value.y, 0.005f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::PopStyleColor(3);
		}
		ImGui::PopStyleVar();
		ImGui::Columns(1);
		ImGui::PopID();
	}

	void KansUI::DrawFloatControl(const std::string& label, float& value, float resetvalue /*= 0.0f*/, float columwidth /*= 100.0f*/)
	{
		auto boldFont = ImGui::GetIO().Fonts->Fonts[1];
		//label
		ImGui::PushID(label.c_str());
		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, columwidth);
		ImGui::Text(label.c_str());
		ImGui::NextColumn();

		//value
		ImGui::PushMultiItemsWidths(1, ImGui::CalcItemWidth());
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
		ImVec2 buttonsize = { ImGui::CalcItemWidth()/3,lineHeight };
		//X
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(221 / 256.0f, 15 / 256.0f, 116 / 256.0f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(221 / 256.0f, 15 / 256.0f, 116 / 256.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(197 / 256.0f, 19 / 256.0f, 104 / 256.0f, 1.0f));

			ImGui::PushFont(boldFont);
			if (ImGui::Button("Value", buttonsize))
				value = resetvalue;
			ImGui::PopFont();
			ImGui::SameLine();
			ImGui::DragFloat("##Value", &value, 0.001f, -10.0f, 10.0f, "%.4f");
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::PopStyleColor(3);
		}
		
		ImGui::PopStyleVar();
		ImGui::Columns(1);
		ImGui::PopID();
	}

	void KansUI::DrawIntControl(const std::string& label,int& value, int resetvalue /*= 0.0f*/, float columwidth /*= 100.0f*/)
	{
		auto boldFont = ImGui::GetIO().Fonts->Fonts[1];
		//label
		ImGui::PushID(label.c_str());
		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, columwidth);
		ImGui::Text(label.c_str());
		ImGui::NextColumn();

		//value
		ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
		float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
		ImVec2 buttonsize = { lineHeight + 3.0f,lineHeight };
		//X
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(221 / 256.0f, 15 / 256.0f, 116 / 256.0f, 0.8f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(221 / 256.0f, 15 / 256.0f, 116 / 256.0f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(197 / 256.0f, 19 / 256.0f, 104 / 256.0f, 1.0f));

			ImGui::PushFont(boldFont);
			if (ImGui::Button("Value", buttonsize))
				value = resetvalue;
			ImGui::PopFont();

			ImGui::SameLine();
			ImGui::DragInt("##Value", &value, 1.0f, 0.0f, 0.0f, "%.2f");
			ImGui::PopItemWidth();
			ImGui::SameLine();
			ImGui::PopStyleColor(3);
		}

		ImGui::PopStyleVar();
		ImGui::Columns(1);
		ImGui::PopID();
	}

}
