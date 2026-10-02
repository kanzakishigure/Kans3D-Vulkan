#include "kspch.h"
#include "SceneHierachyPanel.h"

#include "Kans3D/Scene/Components.h"
#include "Kans3D/ImGui/KansUI.h"

#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Script/ScriptEngine.h"
#include "Kans3D/ImGui/Colors.h"

#include <glm/gtc/type_ptr.hpp>
#include <sstream>

namespace Kans
{
	static bool IsEditableMaterialUniform(ShaderDataType type)
	{
		switch (type) {
		case ShaderDataType::Float:
		case ShaderDataType::Float2:
		case ShaderDataType::Float3:
		case ShaderDataType::Float4:
		case ShaderDataType::Color3:
		case ShaderDataType::Color4:
		case ShaderDataType::Int2:
		case ShaderDataType::Bool:
			return true;
		default:
			return false;
		}
	}

	static void DrawMaterialUniformRow(const std::string& name, const ShaderUniform& uniform,
		const Ref<Material>& material)
	{
		if (!IsEditableMaterialUniform(uniform.GetType()))
			return;
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(name.c_str());
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", name.c_str());
		ImGui::TableSetColumnIndex(1);
		ImGui::PushID(name.c_str());
		ImGui::SetNextItemWidth(-1.0f);
		switch (uniform.GetType()) {
		case ShaderDataType::Float: {
			float value = material->GetFloat(name);
			if (ImGui::DragFloat("##value", &value, 0.01f))
				material->Set(name, value);
			break;
		}
		case ShaderDataType::Float2: {
			glm::vec2 value = material->GetVec2(name);
			if (ImGui::DragFloat2("##value", glm::value_ptr(value), 0.01f))
				material->Set(name, value);
			break;
		}
		case ShaderDataType::Float3: {
			glm::vec3 value = material->GetVec3(name);
			if (ImGui::DragFloat3("##value", glm::value_ptr(value), 0.01f))
				material->Set(name, value);
			break;
		}
		case ShaderDataType::Float4: {
			glm::vec4 value = material->GetVec4(name);
			if (ImGui::DragFloat4("##value", glm::value_ptr(value), 0.01f))
				material->Set(name, value);
			break;
		}
		case ShaderDataType::Color3: {
			glm::vec3 value = material->GetVec3(name);
			if (ImGui::ColorEdit3("##value", glm::value_ptr(value)))
				material->Set(name, value);
			break;
		}
		case ShaderDataType::Color4: {
			glm::vec4 value = material->GetVec4(name);
			if (ImGui::ColorEdit4("##value", glm::value_ptr(value)))
				material->Set(name, value);
			break;
		}
		case ShaderDataType::Int2: {
			glm::ivec2 value = material->GetIVec2(name);
			if (ImGui::DragInt2("##value", glm::value_ptr(value), 1, 0, 255))
				material->Set(name, value);
			break;
		}
		case ShaderDataType::Bool: {
			bool value = material->GetBool(name);
			if (ImGui::Checkbox("##value", &value))
				material->Set(name, value);
			break;
		}
		default:
			break;
		}
		ImGui::PopID();
	}

	static void CopyMaterialUniform(const std::string& name, const ShaderUniform& uniform,
		const Ref<Material>& source, const Ref<Material>& target)
	{
		if (source == target)
			return;
		switch (uniform.GetType()) {
		case ShaderDataType::Float: target->Set(name, source->GetFloat(name)); break;
		case ShaderDataType::Float2: target->Set(name, source->GetVec2(name)); break;
		case ShaderDataType::Float3:
		case ShaderDataType::Color3: target->Set(name, source->GetVec3(name)); break;
		case ShaderDataType::Float4:
		case ShaderDataType::Color4: target->Set(name, source->GetVec4(name)); break;
		case ShaderDataType::Int2: target->Set(name, source->GetIVec2(name)); break;
		case ShaderDataType::Bool: target->Set(name, source->GetBool(name)); break;
		default: break;
		}
	}

	void SceneHierachyPanel::onImGuiRender(bool isOpen )
	{
		
		{
			ImGui::Begin("SceneHierachyPanel:");
			m_Context->m_Registry.each([&](auto entityID)
				{
					Entity entity = { entityID ,m_Context.get() };
					drawEntityNode(entity);
				});

			if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered())
			{
				m_SelectionContext = {};
			}
			//create Entity
			if (ImGui::BeginPopupContextWindow(0,1,false))
			{
				if(ImGui::MenuItem("Create Entity"))
					m_Context->CreateEntity("Empty Entity");
				
				ImGui::EndPopup();
			}
			ImGui::End();
		} 
				
		{
			ImGui::Begin("Properties");
			if (m_SelectionContext)
			{

				drawComponents(m_SelectionContext);
				ImGui::Separator();
				ImGui::SameLine(ImGui::GetWindowWidth()/3);
				if (ImGui::Button("Add Component",ImVec2(ImGui::GetWindowWidth() / 3,20)))
				{
					ImGui::OpenPopup("AddComponent");
				}
				if (ImGui::BeginPopup("AddComponent"))
				{
					if (ImGui::MenuItem("Sprite Component"))
					{
						auto& spritCMP = m_SelectionContext.AddComponent<SpriteRendererComponent>();
						spritCMP.Texture = Kans::Renderer::GetWhiteTexture();
						ImGui::CloseCurrentPopup();
					}
					if (ImGui::MenuItem("Mesh Component"))
					{
						auto& meshCMP = m_SelectionContext.AddComponent<StaticMeshComponent>();
						auto meshSrouce = nullptr;
						meshCMP.StaticMesh = nullptr;
					}
					if (ImGui::MenuItem("Camera Component"))
					{
						m_SelectionContext.AddComponent<CameraComponent>();
						ImGui::CloseCurrentPopup();
					}
					if (ImGui::MenuItem("Script Component"))
					{
						auto& ScriptCMP = m_SelectionContext.AddComponent<ScriptComponent>();
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
			}
				
			ImGui::End();
		}
		
	}
	

	void SceneHierachyPanel::drawEntityNode(Entity entity)
	{
		auto tag = entity.GetComponent<TagComponent>();
		
		ImGuiTreeNodeFlags flag = ( (m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
		//extend selectable area
		flag |= ImGuiTreeNodeFlags_SpanAvailWidth;

		bool opened =	ImGui::TreeNodeEx((void*)(uint32_t)(entity),flag,tag.Tag.c_str());
		
		if (ImGui::IsItemClicked())
		{
			m_SelectionContext = entity;
		}
		bool EntityDelete = false;
		//delete Entity
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Delete Entity"))
			{
				EntityDelete = true;
			}
			ImGui::EndPopup();
		}
		if (opened)
		{
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
			bool opened = ImGui::TreeNodeEx((void*)7589654, flags, tag.Tag.c_str());
			if (opened)
			{
				ImGui::TreePop();
			}
			ImGui::TreePop();
		}
		if (EntityDelete)
		{
			m_Context->DestroyEntity(entity);
			if (m_SelectionContext == entity)
			{
				m_SelectionContext = {};
			}
		}
			
		
	}
	const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_DefaultOpen;
	void SceneHierachyPanel::drawComponents(Entity entity)
	{


		if (entity.HasComponent<IDComponent>())
		{
			auto id = (uint64_t)entity.GetComponent<IDComponent>().ID;
			std::string label = "Entity UUID :";
			std::ostringstream os;
			os << label <<id;
			ImGui::Text(os.str().c_str());
			ImGui::Separator();

		}

		if (entity.HasComponent<TagComponent>())
		{
			
			ImGui::Text("Entity Tag :");
			auto& tag = entity.GetComponent<TagComponent>().Tag;
			char buffer[256];
			memset(buffer, 0, sizeof(buffer));
			strcpy(buffer, tag.c_str());
			if (ImGui::InputText("##Tag", buffer, sizeof(buffer)),ImGuiInputTextFlags_AutoSelectAll)
			{
				tag = std::string(buffer);
			}
			ImGui::Separator();
			
		}

		UI::DrawComponent<TransformComponent>("Transform", entity, [](TransformComponent& component) {
					
			UI::DrawVec3Control("Position", component.Position);

			glm::vec3  rotation = glm::degrees(component.Rotation);
			UI::DrawVec3Control("Rotation", rotation);
			component.Rotation = glm::radians(rotation);

			UI::DrawVec3Control("Scale", component.Scale, 1.0f);
			});	
		UI::DrawComponent<CameraComponent>("Camera", entity, [](CameraComponent& component) {
			auto& camera = component.SceneCamera;
			ImGui::Checkbox("Isprimary", &component.Primary);	
			ImGui::Checkbox("FixedAspectRatio", &component.FixedAspectRatio);
			char* projectiontype[] = { "Perspective","Orthographic" };
			char* curentprojectstring = projectiontype[(int)camera.GetProjectionType()];
			if (ImGui::BeginCombo("Camera ProjectionType", curentprojectstring))
			{

				for (int i = 0; i < 2; i++)
				{
					bool isSelected = curentprojectstring == projectiontype[i];
					if (ImGui::Selectable(projectiontype[i], isSelected))
					{
						curentprojectstring = projectiontype[i];
						camera.SetProjectionType((SceneCamera::ProjectionType)i);
					}
					if (isSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			//Perspective
			if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
			{
				//VerticalFOV
				{
					float verticalFOV = camera.GetPerspectiveVerticalFOV();
					if (ImGui::DragFloat("VerticalFOV", &verticalFOV))
					{
						camera.SetPerspectiveVerticalFOV(verticalFOV);
					}
				}
				//Near Clip
				{
					float nearClip = camera.GetPerspectiveNearClip();
					if (ImGui::DragFloat("Near Clip", &nearClip))
					{
						camera.SetPerspectiveNearClip(nearClip);
					}
				}
				//Far Clip
				{
					float farClip = camera.GetPerspectiveFarClip();
					if (ImGui::DragFloat("Far Clip", &farClip))
					{
						camera.SetPerspectiveFarClip(farClip);
					}
				}
				//Exposure
				{
					float exposure = camera.GetExposure();
					if (ImGui::DragFloat("Exposure", &exposure))
					{
						camera.SetExposure(exposure);
					}
				}

			}
			//Orthographic
			if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
			{
				//Size
				{
					float size = camera.GetOrthographicSize();
					if (ImGui::DragFloat("Size", &size))
					{
						camera.SetOrthographicSize(size);
					}
				}
				//Near Clip
				{
					float nearClip = camera.GetOrthographicNearClip();
					if (ImGui::DragFloat("Near Clip", &nearClip))
					{
						camera.SetOrthographicNearClip(nearClip);
					}
				}
				//Far Clip
				{
					float farClip = camera.GetOrthographicFarClip();
					if (ImGui::DragFloat("Far Clip", &farClip))
					{
						camera.SetOrthographicFarClip(farClip);
					}
				}
				//Exposure
				{
					float exposure = camera.GetExposure();
					if (ImGui::DragFloat("Exposure", &exposure))
					{
						camera.SetExposure(exposure);
					}
				}

			}
			});
		UI::DrawComponent<SpriteRendererComponent>("SpriteRenderer", entity, [](SpriteRendererComponent& component) {
			auto& color = component.Color;
			ImGui::ColorEdit4("Color:", glm::value_ptr(color));
			ImGui::Separator();
			ImGui::Text("Texture path is : %s", component.Texture->GetPath().c_str());
		});
		UI::DrawComponent<DirLightComponent>("DirLight", entity, [](DirLightComponent& component) {
			UI::DrawVec3Control("Direction", component.Direction);
			ImGui::ColorEdit3("Diffuse_Intensity",glm::value_ptr(component.Diffuse_Intensity));
			ImGui::ColorEdit3("Specular_Intensity", glm::value_ptr(component.Specular_Intensity));
			ImGui::ColorEdit3("Ambient_Intensity", glm::value_ptr(component.Ambient_Intensity));
			});
		UI::DrawComponent<PointLightComponent>("PointLight", entity, [](PointLightComponent& component) {
			ImGui::ColorEdit3("Diffuse_Intensity", glm::value_ptr(component.Diffuse_Intensity));
			ImGui::ColorEdit3("Specular_Intensity", glm::value_ptr(component.Specular_Intensity));
			ImGui::ColorEdit3("Ambient_Intensity", glm::value_ptr(component.Ambient_Intensity));
			});
		UI::DrawComponent<StaticMeshComponent>("Mesh", entity, [](StaticMeshComponent& component) {
			if (component.StaticMesh)
			{
				ImGui::Text("Mesh load path is: %s", component.StaticMesh->GetMeshSource()->GetLoadPath().c_str());
			}
			
			});
		UI::DrawComponent<MaterialComponent>("Material", entity, [](MaterialComponent& component) {
			if (!component.MaterialTable) {
				ImGui::TextDisabled("No material table");
				return;
			}
			const uint32_t materialCount = component.MaterialTable->GetMaterialCount();
			if (materialCount == 0) {
				ImGui::TextDisabled("No materials");
				return;
			}

			ImGui::TextDisabled("%u material%s", materialCount, materialCount == 1 ? "" : "s");
			auto firstAsset = component.MaterialTable->GetMaterialAsset(0);
			if (!firstAsset || !firstAsset->GetMaterial())
				return;
			auto firstMaterial = firstAsset->GetMaterial();
			std::map<std::string, ShaderUniform> globalUniforms;
			for (const auto& entry : firstMaterial->GetShaderBuffer().ShaderUniforms)
				if (entry.first.find("U_") != std::string::npos)
					globalUniforms.emplace(entry.first, entry.second);

			if (!globalUniforms.empty()) {
				ImGui::Separator();
				ImGui::TextDisabled("Shared parameters");
				ImGui::PushID("SharedParameters");
				if (ImGui::BeginTable("##Parameters", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
					ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.42f);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
					for (const auto& entry : globalUniforms)
						DrawMaterialUniformRow(entry.first, entry.second, firstMaterial);
					ImGui::EndTable();
				}
				ImGui::PopID();
			}

			ImGui::Separator();
			if (ImGui::BeginTabBar("MaterialTabBar", ImGuiTabBarFlags_FittingPolicyScroll)) {
				for (uint32_t i = 0; i < materialCount; ++i) {
					auto asset = component.MaterialTable->GetMaterialAsset(i);
					if (!asset || !asset->GetMaterial())
						continue;
					auto material = asset->GetMaterial();
					for (const auto& entry : material->GetShaderBuffer().ShaderUniforms) {
						if (globalUniforms.find(entry.first) != globalUniforms.end())
							CopyMaterialUniform(entry.first, entry.second, firstMaterial, material);
					}
					const std::string tabLabel = material->GetName() + "##material" + std::to_string(i);
					if (!ImGui::BeginTabItem(tabLabel.c_str()))
						continue;
					ImGui::PushID(static_cast<int>(i));
					ImGui::TextDisabled("Shader");
					ImGui::SameLine();
					ImGui::TextWrapped("%s", material->GetShader()->GetName().c_str());

					std::map<std::string, ShaderUniform> localUniforms;
					for (const auto& entry : material->GetShaderBuffer().ShaderUniforms)
						if (globalUniforms.find(entry.first) == globalUniforms.end())
							localUniforms.emplace(entry.first, entry.second);
					if (!localUniforms.empty()) {
						ImGui::Separator();
						ImGui::TextDisabled("Parameters");
						if (ImGui::BeginTable("##Parameters", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
							ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.42f);
							ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
							for (const auto& entry : localUniforms)
								DrawMaterialUniformRow(entry.first, entry.second, material);
							ImGui::EndTable();
						}
					}

					if (!material->GetTextures().empty()) {
						ImGui::Separator();
						ImGui::TextDisabled("Textures");
						for (auto& texture : material->GetTextures()) {
							ImGui::PushID(texture.first.c_str());
							const float previewSize = std::min(96.0f, std::max(48.0f, ImGui::GetContentRegionAvail().x * 0.3f));
							if (texture.second) {
								ImGui::Image((ImTextureID)(uintptr_t)texture.second->GetRenererID(), ImVec2(previewSize, previewSize));
							} else {
								ImGui::Button("Drop texture", ImVec2(previewSize, previewSize));
							}
							if (ImGui::BeginDragDropTarget()) {
								if (const auto* payload = ImGui::AcceptDragDropPayload("asset_payload")) {
									std::string path((const char*)payload->Data, payload->DataSize);
									TextureSpecification spec;
									texture.second = Texture2D::Create(spec, path);
								}
								ImGui::EndDragDropTarget();
							}
							ImGui::SameLine();
							ImGui::BeginGroup();
							ImGui::TextWrapped("%s", texture.first.c_str());
							if (texture.second) {
								ImGui::TextDisabled("%u x %u", texture.second->GetWidth(), texture.second->GetHeight());
								ImGui::TextWrapped("%s", texture.second->GetPath().filename().string().c_str());
							}
							ImGui::EndGroup();
							ImGui::PopID();
						}
					}
					ImGui::PopID();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		});
		UI::DrawComponent<ScriptComponent>("Script", entity, [](ScriptComponent& component) {
			ImGui::Text("Class :");
			char buffer[256] = {0};
			auto& name = component.ClassName;
			strcat(buffer, name.c_str());
			if (ScriptEngine::EntityClassExists(name))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colors::Theme::text));
			}
			else
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colors::Theme::red_6));
			}
			if (ImGui::InputText("##Name", buffer, sizeof(buffer)))
			{
				name = std::string(buffer);
			}

			ImGui::PopStyleColor();
			});
		
	}
	

}

