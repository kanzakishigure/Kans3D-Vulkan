#include "kspch.h"
#include "SceneHierachyPanel.h"

#include "kans3D/Scene/Components.h"
#include "kans3D/ImGui/KansUI.h"

#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Script/ScriptEngine.h"
#include "Kans3D/ImGui/Colors.h"

#include <glm/gtc/type_ptr.hpp>
#include <sstream>

namespace Kans
{

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
			strcpy_s(buffer, sizeof(buffer) ,tag.c_str());
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
			const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_Framed
				| ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
			auto materialCount = component.MaterialTable->GetMaterialCount();

			// Child 1: no border, enable horizontal scrollbar
			{
				ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar;
				if (false)
					window_flags |= ImGuiWindowFlags_NoScrollWithMouse;
				ImGui::BeginChild("ChildL", ImVec2(ImGui::GetContentRegionAvail().x , ImGui::GetContentRegionAvail().y*0.5), false, window_flags);
				
				
			}
			////////////////////////////////////////////////////

			std::map<std::string, ShaderUniform> GlobelShaderUniforms;
			if (materialCount > 0)
			{
				auto& material = component.MaterialTable->GetMaterialAsset(0)->GetMaterial();
				auto& materialBuffer = material->GetShaderBuffer();

				for (auto& UniformMap : materialBuffer.ShaderUniforms)
				{
					const std::string& uniformName = UniformMap.first;
					auto& uniform = UniformMap.second;
					if (uniformName.find("U_") != uniformName.npos)
					{
						GlobelShaderUniforms[uniformName] = uniform;
						switch (uniform.GetType())
						{
						case ShaderDataType::Float:
						{
							float value = 0.0f;
							value = material->GetFloat(uniformName);
							std::string label = uniformName;
							UI::DrawFloatControl(label, value);
							material->Set(uniformName, value);
						}break;
						case ShaderDataType::Float2:
						{
							glm::vec2 value = glm::vec2(1.0);
							value = material->GetVec2(uniformName);
							std::string label = uniformName;
							UI::DrawVec2Control(label, value);
							material->Set(uniformName, value);
						}break;
						case ShaderDataType::Float3:
						{
							glm::vec3 value = glm::vec3(1.0);
							value = material->GetVec3(uniformName);
							std::string label = uniformName;
							UI::DrawVec3Control(label, value);
							material->Set(uniformName, value);
						}break;
						case ShaderDataType::Bool:
						{
							bool value = false;
							value = material->GetBool(uniformName);
							std::string label = uniformName;
							ImGui::Checkbox(label.c_str(), &value);
							material->Set(uniformName, value);
						}break;
						}
						
					}
				}
			}
			ImGui::Separator();
			////////////////////////////////////////////////////
			for (uint32_t i = 0; i < materialCount; i++)
			{
				
				auto& material = component.MaterialTable->GetMaterialAsset(i)->GetMaterial();
				auto& materialBuffer = material->GetShaderBuffer();

				ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
				auto contentregion = ImGui::GetContentRegionAvail();
				float lineHeight = GImGui->Font->FontSize + GImGui->Style.FramePadding.y * 2.0f;
				
				ImGui::PopStyleVar();
				
				////////////////////////////////////////////////////
				ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
				if (ImGui::BeginTabBar("MaterialTabBar", tab_bar_flags))
				{
					//set the global uniform Value
					for (auto& UniformMap : materialBuffer.ShaderUniforms)
					{
						const std::string& uniformName = UniformMap.first;
						auto& uniform = UniformMap.second;

						if (GlobelShaderUniforms.find(uniformName) != GlobelShaderUniforms.end())
						{
							switch (uniform.GetType())
							{
							case ShaderDataType::Float:
							{
								float value = 0.0f;
								value = component.MaterialTable->GetMaterialAsset(0)->GetMaterial()->GetFloat(uniformName);
								material->Set(uniformName, value);
							}break;
							case ShaderDataType::Float2:
							{
								glm::vec2 value = glm::vec2(1.0);
								value = component.MaterialTable->GetMaterialAsset(0)->GetMaterial()->GetVec2(uniformName);
								material->Set(uniformName, value);
							}break;
							case ShaderDataType::Float3:
							{
								glm::vec3 value = glm::vec3(1.0);
								value = component.MaterialTable->GetMaterialAsset(0)->GetMaterial()->GetVec3(uniformName);
								material->Set(uniformName, value);
							}break;
							case ShaderDataType::Bool:
							{
								bool value = false;
								value = component.MaterialTable->GetMaterialAsset(0)->GetMaterial()->GetBool(uniformName);
								material->Set(uniformName, value);
							}break;
							}
						}
					}
					//set the  native uniform Value
					if (ImGui::BeginTabItem(material->GetName().c_str()))
					{
						ImGui::Text("Shader : %s", material->GetShader()->GetName().c_str());
						ImGui::NewLine();
						for (auto& UniformMap : materialBuffer.ShaderUniforms)
						{

							const std::string& uniformName = UniformMap.first;
							auto& uniform = UniformMap.second;

							if (GlobelShaderUniforms.find(uniformName) != GlobelShaderUniforms.end())
							{
								continue;
							}
							ImGui::Separator();
							switch (uniform.GetType())
							{
							case ShaderDataType::Float:
							{
								float value = 0.0f;
								value = material->GetFloat(uniformName);
								std::string label = material->GetName() + ":[ " + uniformName + " ]";

								UI::DrawFloatControl(label, value);
								material->Set(uniformName, value);
							}
							break;
							case ShaderDataType::Float2:
							{
								glm::vec2 value = glm::vec2(1.0);
								value = material->GetVec2(uniformName);
								std::string label = material->GetName() + ":[ " + uniformName + " ]";
								UI::DrawVec2Control(label, value);
								material->Set(uniformName, value);
							}
							break;
							case ShaderDataType::Float3:
							{
								glm::vec3 value = glm::vec3(1.0);
								value = material->GetVec3(uniformName);
								std::string label = material->GetName() + ":[ " + uniformName + " ]";
								UI::DrawVec3Control(label, value);
								material->Set(uniformName, value);
							}
							break;
							case ShaderDataType::Float4:
							{
								glm::vec4 value = glm::vec4(1.0);
								value = material->GetVec4(uniformName);
								std::string label = material->GetName() + ":[ " + uniformName + " ]";
								UI::DrawVec4Control(label, value);
								material->Set(uniformName, value);
							}
							break;
							case ShaderDataType::Color4:
							{
								glm::vec4 value = glm::vec4(1.0);
								value = material->GetVec4(uniformName);
								std::string label = material->GetName() + ":[ " + uniformName + " ]";
								ImGui::ColorEdit4(label.c_str(), glm::value_ptr(value));
								material->Set(uniformName, value);
							}
							break;
							case ShaderDataType::Color3:
							{
								glm::vec3 value = glm::vec3(1.0);
								value = material->GetVec4(uniformName);
								std::string label = material->GetName() + ":[ " + uniformName + " ]";
								ImGui::ColorEdit3(label.c_str(), glm::value_ptr(value));
								material->Set(uniformName, value);
							}
							break;
							case ShaderDataType::Int2:
							{
								glm::ivec2 value = glm::ivec2(0);
								value = material->GetIVec2(uniformName);
								std::string label = material->GetName() + ":[ " + uniformName + " ]";
								ImGui::DragInt2(label.c_str(), glm::value_ptr(value), 1, 0, 255);
								material->Set(uniformName, value);
							}
							break;
							case ShaderDataType::Bool:
							{
								bool value = false;
								value = material->GetBool(uniformName);
								std::string label = material->GetName() + ":[ " + uniformName + " ]";
								ImGui::Checkbox(label.c_str(), &value);
								material->Set(uniformName, value);
							}
							break;

							}
						}
						for (auto& texture : material->GetTextures())
						{
							
							ImGui::Text(texture.first.c_str());
							ImGui::BeginGroup();
							ImGui::Image((void*)texture.second->GetRenererID(), ImVec2(150, 150));
							
							if (ImGui::BeginDragDropTarget( ))
							{
								auto data = ImGui::AcceptDragDropPayload("asset_payload");
								if (data)
								{
									std::string path = std::string((char*)data->Data,data->DataSize);
									TextureSpecification spec;
									texture.second = Texture2D::Create(spec, path);
								}
								ImGui::EndDragDropTarget();	
							}
							ImGui::EndGroup();
							ImGui::Separator();
						}
						ImGui::EndTabItem();
					}
					ImGui::EndTabBar();
				}
				////////////////////////////////////////////////////
				
				ImGui::Separator();
			}
			ImGui::EndChild();
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

