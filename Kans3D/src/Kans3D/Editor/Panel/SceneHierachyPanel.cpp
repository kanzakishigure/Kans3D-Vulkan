#include "kspch.h"
#include "SceneHierachyPanel.h"

#include "Kans3D/Asset/Runtime/MeshData.h"
#include "Kans3D/Asset/Runtime/ModelData.h"
#include "Kans3D/ImGui/KansUI.h"
#include "Kans3D/Scene/Components.h"

#include "Kans3D/ImGui/Colors.h"
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Script/ScriptEngine.h"

#include <sstream>
#include <glm/gtc/type_ptr.hpp>

namespace Kans
{
    void SceneHierachyPanel::onImGuiRender()
    {
        if (!m_Context)
            return;
        {
            m_Context->m_Registry.each([&](auto entityID) {
                Entity entity = {entityID, m_Context.get()};
                drawEntityNode(entity);
            });

            if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered())
            {
                m_SelectionContext = {};
            }
            // create Entity
            if (ImGui::BeginPopupContextWindow(0, 1, false))
            {
                if (ImGui::MenuItem("Create Entity"))
                    m_Context->CreateEntity("Empty Entity");

                ImGui::EndPopup();
            }
        }
    }

    void SceneHierachyPanel::drawEntityNode(Entity entity)
    {
        auto tag = entity.GetComponent<TagComponent>();

        ImGuiTreeNodeFlags flag =
            ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
        // extend selectable area
        flag |= ImGuiTreeNodeFlags_SpanAvailWidth;

        bool opened = ImGui::TreeNodeEx((void*)(uint32_t)(entity), flag, tag.Tag.c_str());

        if (ImGui::IsItemClicked())
        {
            m_SelectionContext = entity;
        }
        bool EntityDelete = false;
        // delete Entity
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
            ImGuiTreeNodeFlags flags  = ImGuiTreeNodeFlags_OpenOnArrow;
            bool               opened = ImGui::TreeNodeEx((void*)7589654, flags, tag.Tag.c_str());
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
} // namespace Kans
