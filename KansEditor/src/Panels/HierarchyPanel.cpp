#include "HierarchyPanel.h"
#include <algorithm>
#include <cctype>
#include <imgui.h>
#include <imgui_internal.h>
#include "Kans3D/Renderer/Renderer.h"
#include "Kans3D/Scene/Components.h"
#include "UI/EditorIcons.h"

namespace Kans
{
    namespace
    {
        constexpr const char* EntityPayload = "entity_payload";
        constexpr ImU32       DropColor     = IM_COL32(70, 150, 230, 255);

        std::string Lowercase(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return text;
        }

        // The most telling component decides the icon.
        const char* IconFor(Entity entity)
        {
            if (entity.HasComponent<CameraComponent>())
                return ICON_FA_VIDEO;
            if (entity.HasComponent<DirLightComponent>())
                return ICON_FA_SUN;
            if (entity.HasComponent<PointLightComponent>())
                return ICON_FA_LIGHTBULB;
            if (entity.HasComponent<PrefabInstanceComponent>())
                return ICON_FA_CUBES;
            if (entity.HasComponent<MeshComponent>())
                return ICON_FA_CUBE;
            if (entity.HasComponent<SpriteRendererComponent>())
                return ICON_FA_IMAGE;
            if (entity.HasComponent<ScriptComponent>())
                return ICON_FA_CODE;
            return ICON_FA_VECTOR_SQUARE;
        }

        std::string Label(const char* icon, const char* text) { return std::string(icon) + "  " + text; }
    } // namespace

    void HierarchyPanel::SetScene(Ref<Scene> scene)
    {
        m_Scene    = std::move(scene);
        m_Selected = 0;
        m_Renaming = 0;
        m_Reveal   = 0;
    }

    Entity HierarchyPanel::GetSelected() const
    {
        return m_Scene && m_Selected ? m_Scene->GetEntityByUUID(m_Selected) : Entity {};
    }

    void HierarchyPanel::SetSelected(Entity entity)
    {
        m_Selected = entity ? entity.GetUUID() : UUID(0);
        m_Reveal   = m_Selected;
    }

    // ---------------------------------------------------------------------- drawing

    void HierarchyPanel::Draw()
    {
        if (!m_Scene)
        {
            ImGui::TextDisabled("No scene is open.");
            return;
        }
        DrawToolbar();
        ImGui::Separator();

        m_Matches.clear();
        const bool filtering = m_Search[0] != 0;
        const auto roots     = m_Scene->GetRootEntities();
        if (filtering)
            for (Entity root : roots)
                UpdateMatches(root);

        ImGui::BeginChild("##Entities");
        m_Order.clear();
        for (Entity root : roots)
            if (!filtering || m_Matches[uint64_t(root.GetUUID())])
                DrawEntity(root);
        m_Reveal = 0;
        if (roots.empty())
            ImGui::TextDisabled("The scene is empty. Right-click to create an entity.");
        else if (filtering && m_Order.empty())
            ImGui::TextDisabled("No entity matches the search.");

        // Empty area: deselect, create, and drop to move to the top level.
        ImGui::Dummy(ImGui::GetContentRegionAvail());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            CommitRename();
            m_Selected = 0;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            m_OpenCreateMenu = true;
        AcceptDrop({});
        HandleKeyboard();

        if (m_OpenEntityMenu)
            ImGui::OpenPopup("##EntityMenu");
        if (m_OpenCreateMenu)
            ImGui::OpenPopup("##CreateMenu");
        m_OpenEntityMenu = m_OpenCreateMenu = false;
        if (ImGui::BeginPopup("##EntityMenu"))
        {
            if (Entity entity = m_Scene->GetEntityByUUID(m_MenuEntity))
                DrawEntityMenu(entity);
            ImGui::EndPopup();
        }
        if (ImGui::BeginPopup("##CreateMenu"))
        {
            DrawCreateItems({});
            ImGui::EndPopup();
        }
        ImGui::EndChild();
    }

    void HierarchyPanel::DrawToolbar()
    {
        if (ImGui::Button(ICON_FA_PLUS))
            ImGui::OpenPopup("##CreateToolbar");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Create an entity (as a child of the selection)");
        if (ImGui::BeginPopup("##CreateToolbar"))
        {
            DrawCreateItems(GetSelected());
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##search", ICON_FA_MAGNIFYING_GLASS " Search", m_Search.data(), m_Search.size());
    }

    bool HierarchyPanel::UpdateMatches(Entity entity)
    {
        bool matches = Lowercase(entity.GetComponent<TagComponent>().Tag).find(Lowercase(m_Search.data())) !=
                       std::string::npos;
        for (Entity child : m_Scene->GetChildren(entity))
            matches |= UpdateMatches(child);
        m_Matches[uint64_t(entity.GetUUID())] = matches;
        return matches;
    }

    void HierarchyPanel::DrawEntity(Entity entity)
    {
        const UUID  id        = entity.GetUUID();
        const auto  children  = m_Scene->GetChildren(entity);
        const bool  filtering = m_Search[0] != 0;
        const auto& name      = entity.GetComponent<TagComponent>().Tag;
        m_Order.push_back(id);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                   ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowItemOverlap;
        if (id == m_Selected)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (children.empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        // Open the path to a revealed entity, and every match while searching.
        const Entity revealed = m_Reveal ? m_Scene->GetEntityByUUID(m_Reveal) : Entity {};
        if (filtering || (revealed && m_Scene->IsDescendantOf(revealed, entity)))
            ImGui::SetNextItemOpen(true);

        const bool renaming = id == m_Renaming;
        const bool direct   = !filtering || Lowercase(name).find(Lowercase(m_Search.data())) != std::string::npos;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4.0f, 2.0f});
        // Prefab instances and their parts are tinted, as their structure comes from the prefab.
        const bool owned  = entity.HasComponent<PrefabOwnedComponent>();
        const bool prefab = owned || entity.HasComponent<PrefabInstanceComponent>();
        if (!direct)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        else if (prefab)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.70f, 1.0f, 1.0f));
        const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(uintptr_t(uint64_t(id))),
                                            flags,
                                            "%s  %s",
                                            IconFor(entity),
                                            renaming ? "" : name.c_str());
        if (!direct || prefab)
            ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
        {
            if (m_Renaming != id)
                CommitRename();
            m_Selected = id;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !owned)
                StartRename(entity);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            m_Selected       = id;
            m_MenuEntity     = id;
            m_OpenEntityMenu = true;
        }
        // A prefab part stays where the prefab puts it; it cannot be moved out.
        if (!owned && ImGui::BeginDragDropSource())
        {
            const uint64_t payload = uint64_t(id);
            ImGui::SetDragDropPayload(EntityPayload, &payload, sizeof(payload));
            ImGui::Text("%s  %s", IconFor(entity), name.c_str());
            ImGui::EndDragDropSource();
        }
        AcceptDrop(entity);

        if (renaming)
        {
            // Edit the name in place, after the icon.
            ImGui::SameLine(ImGui::GetItemRectMin().x - ImGui::GetWindowPos().x + ImGui::GetTreeNodeToLabelSpacing() +
                            ImGui::CalcTextSize(IconFor(entity)).x + 8.0f);
            if (m_RenameFocus)
            {
                ImGui::SetKeyboardFocusHere();
                m_RenameFocus = false;
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool submitted = ImGui::InputText("##rename",
                                                    m_RenameBuffer.data(),
                                                    m_RenameBuffer.size(),
                                                    ImGuiInputTextFlags_EnterReturnsTrue |
                                                        ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape))
                m_Renaming = 0;
            else if (submitted || ImGui::IsItemDeactivated())
                CommitRename();
        }

        if (!open)
            return;
        for (Entity child : children)
            if (!filtering || m_Matches[uint64_t(child.GetUUID())])
                DrawEntity(child);
        ImGui::TreePop();
    }

    void HierarchyPanel::AcceptDrop(Entity target)
    {
        if (!ImGui::BeginDragDropTarget())
            return;
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        // Rows: top quarter inserts before, bottom quarter after, the middle nests.
        DropPlace place = DropPlace::Into;
        if (target)
        {
            const float y = ImGui::GetMousePos().y;
            if (y < min.y + (max.y - min.y) * 0.25f)
                place = DropPlace::Before;
            else if (y > max.y - (max.y - min.y) * 0.25f)
                place = DropPlace::After;
        }
        const ImGuiDragDropFlags flags = ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect;
        if (const auto* payload = ImGui::AcceptDragDropPayload(EntityPayload, flags))
        {
            Entity dragged = m_Scene->GetEntityByUUID(UUID(*static_cast<const uint64_t*>(payload->Data)));
            // An entity cannot move into (or next to an entity inside) its own subtree.
            const bool valid = dragged && dragged != target && !(target && m_Scene->IsDescendantOf(target, dragged));
            if (valid && target)
            {
                ImDrawList* draw = ImGui::GetWindowDrawList();
                if (place == DropPlace::Into)
                    draw->AddRect(min, max, DropColor, 3.0f, 0, 2.0f);
                else
                {
                    const float y = place == DropPlace::Before ? min.y : max.y;
                    draw->AddLine({min.x, y}, {max.x, y}, DropColor, 2.0f);
                }
            }
            if (valid && payload->IsDelivery())
                Move(dragged, target, place);
        }
        // Files from the content browser (e.g. models to place).
        if (const auto* payload = ImGui::AcceptDragDropPayload("asset_payload"))
            if (m_FileDrop)
                m_FileDrop(std::filesystem::u8path(std::string(static_cast<const char*>(payload->Data),
                                                               size_t(payload->DataSize))),
                           target);
        ImGui::EndDragDropTarget();
    }

    void HierarchyPanel::Move(Entity entity, Entity target, DropPlace place)
    {
        if (!target)
        {
            m_Scene->SetParent(entity, {}, {}, true);
            return;
        }
        if (place == DropPlace::Into)
        {
            m_Scene->SetParent(entity, target, {}, true);
            m_Reveal = entity.GetUUID();
            return;
        }
        // Before/after the target among its siblings.
        Entity parent   = m_Scene->GetParent(target);
        auto   siblings = parent ? m_Scene->GetChildren(parent) : m_Scene->GetRootEntities();
        Entity before   = target;
        if (place == DropPlace::After)
        {
            const auto at = std::find(siblings.begin(), siblings.end(), target);
            before        = at != siblings.end() && at + 1 != siblings.end() ? *(at + 1) : Entity {};
            if (before == entity)
            {
                // Already right after the target.
                return;
            }
        }
        m_Scene->SetParent(entity, parent, before, true);
    }

    // ---------------------------------------------------------------------- menus and actions

    void HierarchyPanel::DrawCreateItems(Entity parent)
    {
        if (ImGui::MenuItem(Label(ICON_FA_VECTOR_SQUARE, "Empty Entity").c_str()))
            Create("Entity", parent);
        ImGui::Separator();
        if (ImGui::MenuItem(Label(ICON_FA_CUBE, "Model").c_str()))
            Create("Model", parent).AddComponent<PrefabInstanceComponent>();
        if (ImGui::MenuItem(Label(ICON_FA_VIDEO, "Camera").c_str()))
            Create("Camera", parent).AddComponent<CameraComponent>().Primary = false;
        if (ImGui::MenuItem(Label(ICON_FA_SUN, "Directional Light").c_str()))
        {
            auto& light             = Create("Directional Light", parent).AddComponent<DirLightComponent>();
            light.Direction         = {0.0f, -1.0f, 0.0f};
            light.Diffuse_Intensity = light.Specular_Intensity = light.Ambient_Intensity = glm::vec3(1.0f);
        }
        if (ImGui::MenuItem(Label(ICON_FA_LIGHTBULB, "Point Light").c_str()))
        {
            auto& light             = Create("Point Light", parent).AddComponent<PointLightComponent>();
            light.Diffuse_Intensity = light.Specular_Intensity = light.Ambient_Intensity = glm::vec3(1.0f);
        }
        if (ImGui::MenuItem(Label(ICON_FA_IMAGE, "Sprite").c_str()))
            Create("Sprite", parent).AddComponent<SpriteRendererComponent>().Texture = Renderer::GetWhiteTexture();
    }

    void HierarchyPanel::DrawEntityMenu(Entity entity)
    {
        if (ImGui::BeginMenu(Label(ICON_FA_PLUS, "Create Child").c_str()))
        {
            DrawCreateItems(entity);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        const bool owned = entity.HasComponent<PrefabOwnedComponent>();
        if (ImGui::MenuItem(Label(ICON_FA_PEN, "Rename").c_str(), "F2", false, !owned))
            StartRename(entity);
        if (ImGui::MenuItem(Label(ICON_FA_CLONE, "Duplicate").c_str(), "Ctrl+D"))
            Duplicate(entity);
        if (m_Scene->GetParent(entity) && !owned &&
            ImGui::MenuItem(Label(ICON_FA_ARROW_UP, "Move to Top Level").c_str()))
            m_Scene->SetParent(entity, {}, {}, true);
        ImGui::Separator();
        if (ImGui::MenuItem(Label(ICON_FA_COPY, "Copy ID").c_str()))
            ImGui::SetClipboardText(std::to_string(uint64_t(entity.GetUUID())).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.40f, 1.0f));
        const bool children = !m_Scene->GetChildren(entity).empty();
        if (ImGui::MenuItem(Label(ICON_FA_TRASH, children ? "Delete with Children" : "Delete").c_str(), "Del"))
            Delete(entity);
        ImGui::PopStyleColor();
    }

    Entity HierarchyPanel::Create(const char* name, Entity parent)
    {
        Entity entity = m_Scene->CreateEntity(name);
        if (parent)
            m_Scene->SetParent(entity, parent, {}, false);
        SetSelected(entity);
        return entity;
    }

    void HierarchyPanel::Delete(Entity entity)
    {
        const UUID id = entity.GetUUID();
        if (m_Selected == id || (GetSelected() && m_Scene->IsDescendantOf(GetSelected(), entity)))
            m_Selected = 0;
        if (m_Renaming == id)
            m_Renaming = 0;
        m_Scene->DestroyEntity(entity);
    }

    void HierarchyPanel::Duplicate(Entity entity) { SetSelected(m_Scene->DuplicateEntity(entity)); }

    void HierarchyPanel::StartRename(Entity entity)
    {
        // A part's name comes from the prefab and is restored on every expansion.
        if (entity.HasComponent<PrefabOwnedComponent>())
            return;
        m_Renaming = entity.GetUUID();
        m_RenameBuffer.fill(0);
        const auto& name = entity.GetComponent<TagComponent>().Tag;
        std::copy_n(name.begin(), std::min(name.size(), m_RenameBuffer.size() - 1), m_RenameBuffer.begin());
        m_RenameFocus = true;
        m_Selected    = m_Renaming;
    }

    void HierarchyPanel::CommitRename()
    {
        if (!m_Renaming)
            return;
        if (Entity entity = m_Scene->GetEntityByUUID(m_Renaming); entity && m_RenameBuffer[0])
            entity.GetComponent<TagComponent>().Tag = m_RenameBuffer.data();
        m_Renaming = 0;
    }

    void HierarchyPanel::HandleKeyboard()
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || io.WantTextInput || m_Renaming)
            return;
        Entity selected = GetSelected();
        if (!selected)
            return;
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
            StartRename(selected);
        else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            Delete(selected);
        else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            Duplicate(selected);
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        {
            // Move through the entities as shown.
            const auto at = std::find(m_Order.begin(), m_Order.end(), m_Selected);
            if (at == m_Order.end())
                return;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && at != m_Order.begin())
                m_Selected = *(at - 1);
            else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && at + 1 != m_Order.end())
                m_Selected = *(at + 1);
        }
    }
} // namespace Kans
