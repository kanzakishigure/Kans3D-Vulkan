#include "PropertiesPanel.h"
#include <filesystem>
#include "Components/ComponentEditors.h"
#include <algorithm>
#include <cctype>
#include <imgui.h>
#include "Kans3D/Asset/Runtime/MeshData.h"
#include "Kans3D/Renderer/Renderer.h"
#include "UI/EditorIcons.h"
#include "UI/PropertyGrid.h"

namespace Kans
{
    namespace
    {
        constexpr ImVec4 OverrideColor {0.40f, 0.65f, 1.0f, 1.0f};

        bool ContainsIgnoreCase(const std::string& text, const char* needle)
        {
            std::string a(text), b(needle);
            std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            return a.find(b) != std::string::npos;
        }

        // Search box at the top of a popup or combo list.
        void FilterBox(std::array<char, 128>& filter)
        {
            if (ImGui::IsWindowAppearing())
            {
                filter.fill(0);
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS " Search", filter.data(), filter.size());
        }
    } // namespace

    // ---------------------------------------------------------------------- layout

    void PropertiesPanel::Draw()
    {
        Entity entity = m_Hierarchy.GetSelected();
        if (!entity)
        {
            if (Scene* scene = GetScene())
                DrawSceneSettings(*scene);
            ImGui::Spacing();
            ImGui::TextDisabled("Select an entity in the hierarchy to edit its components.");
            return;
        }
        DrawHeader(entity);
        DrawPrefabPart(entity);
        ImGui::Spacing();
        for (const auto& component : ComponentRegistry::Get().Entries())
            if (component.Has(entity))
                DrawSection(entity, component);
        ImGui::Spacing();
        DrawAddComponent(entity);
    }

    void PropertiesPanel::DrawSceneSettings(Scene& scene)
    {
        if (!ImGui::CollapsingHeader(ICON_FA_GLOBE "  Environment", ImGuiTreeNodeFlags_DefaultOpen))
            return;
        // The project's HDR images, listed once per panel open is enough; rescan on demand.
        if (m_EnvironmentMaps.empty() || ImGui::IsWindowAppearing())
        {
            m_EnvironmentMaps.clear();
            std::error_code error;
            const auto      root = std::filesystem::current_path(error);
            for (auto it = std::filesystem::recursive_directory_iterator(root / "assets", error);
                 !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
                if (it->is_regular_file(error) && it->path().extension() == ".hdr")
                    m_EnvironmentMaps.push_back(it->path().lexically_relative(root));
            std::sort(m_EnvironmentMaps.begin(), m_EnvironmentMaps.end());
        }
        const bool authored = scene.HasEnvironmentSettings();
        auto       settings = scene.GetEnvironmentSettings();
        if (!PropertyGrid::Begin("##Environment"))
            return;
        // "Editor default" leaves the scene without settings: it shows the editor's environment.
        const std::string current = !authored            ? "Editor default" :
                                    settings.Map.empty() ? "None" :
                                                           settings.Map.generic_u8string();
        PropertyGrid::Row("Map", "The HDR image lighting the scene and shown as the sky");
        ImGui::PushID("Map");
        if (ImGui::BeginCombo("##map", current.c_str()))
        {
            if (ImGui::Selectable("Editor default", !authored))
                scene.ClearEnvironmentSettings();
            if (ImGui::Selectable("None", authored && settings.Map.empty()))
            {
                settings.Map.clear();
                scene.SetEnvironmentSettings(settings);
            }
            for (const auto& map : m_EnvironmentMaps)
                if (ImGui::Selectable(map.generic_u8string().c_str(), authored && settings.Map == map))
                {
                    settings.Map = map;
                    scene.SetEnvironmentSettings(settings);
                }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        bool changed = PropertyGrid::Float("Diffuse", settings.DiffuseIntensity, 0.01f, 0.0f, 16.0f);
        changed |= PropertyGrid::Float("Specular", settings.SpecularIntensity, 0.01f, 0.0f, 16.0f);
        changed |= PropertyGrid::Float("Rotation", settings.Rotation, 0.5f, -360.0f, 720.0f, "%.1f deg");
        changed |= PropertyGrid::Checkbox("Show sky", settings.ShowSkybox);
        if (settings.ShowSkybox)
            changed |= PropertyGrid::Float("Sky brightness", settings.SkyboxIntensity, 0.01f, 0.0f, 16.0f);
        if (changed)
        {
            // The first edit of a scene without settings keeps the map it was showing.
            if (!authored)
                settings.Map = m_DefaultEnvironment;
            scene.SetEnvironmentSettings(settings);
        }
        PropertyGrid::End();
    }

    void PropertiesPanel::DrawHeader(Entity entity)
    {
        auto& tag = entity.GetComponent<TagComponent>().Tag;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(ICON_FA_CUBE);
        ImGui::SameLine();
        char name[256] = {};
        tag.copy(name, sizeof(name) - 1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputText("##Name", name, sizeof(name)))
            tag = name;
        if (entity.HasComponent<IDComponent>())
        {
            const auto id = std::to_string(static_cast<uint64_t>(entity.GetUUID()));
            ImGui::TextDisabled("ID %s", id.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_COPY "##CopyID"))
                ImGui::SetClipboardText(id.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Copy the entity ID");
        }
    }

    void PropertiesPanel::DrawSection(Entity entity, const ComponentEntry& component)
    {
        Scene*     scene      = GetScene();
        const bool overridden = scene && component.Overridden && component.Overridden(entity, *scene);
        ImGui::PushID(component.Name.c_str());
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                                         ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap |
                                         ImGuiTreeNodeFlags_FramePadding;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6.0f, 4.0f});
        // A prefab part's section that carries an override is tinted, as in the Hierarchy.
        if (overridden)
            ImGui::PushStyleColor(ImGuiCol_Text, OverrideColor);
        const bool open = ImGui::TreeNodeEx(
            "##section", flags, "%s  %s%s", component.Icon, component.Name.c_str(), overridden ? "  (overridden)" : "");
        if (overridden)
            ImGui::PopStyleColor();
        const bool  menu   = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        const float height = ImGui::GetFrameHeight();
        ImGui::PopStyleVar();
        // Settings button at the right end of the header.
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - height);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button(ICON_FA_GEAR, {height, height}) || menu)
            ImGui::OpenPopup("##SectionMenu");
        ImGui::PopStyleColor();
        bool remove = false;
        if (ImGui::BeginPopup("##SectionMenu"))
        {
            bool any = false;
            if (component.Reset && ImGui::MenuItem(ICON_FA_ROTATE_RIGHT "  Reset"))
                component.Reset(entity);
            any |= bool(component.Reset);
            if (component.Copy)
            {
                if (ImGui::MenuItem(ICON_FA_COPY "  Copy values"))
                {
                    m_Clipboard     = component.Copy(entity);
                    m_ClipboardType = component.Type;
                }
                if (ImGui::MenuItem(ICON_FA_PASTE "  Paste values", nullptr, false,
                                    component.Paste && m_ClipboardType == component.Type))
                    component.Paste(entity, m_Clipboard);
                any = true;
            }
            if (component.Revert && ImGui::MenuItem(ICON_FA_ROTATE_LEFT "  Revert to prefab", nullptr, false, overridden))
                component.Revert(entity, *scene);
            any |= overridden;
            const bool removable = component.CanRemove && component.CanRemove(entity);
            if (removable)
            {
                if (any)
                    ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_TRASH "  Remove Component"))
                    remove = true;
            }
            if (!any && !removable)
                ImGui::TextDisabled("No actions");
            ImGui::EndPopup();
        }
        if (open)
        {
            ImGui::Spacing();
            component.Draw(entity, *this);
            ImGui::Spacing();
            ImGui::TreePop();
        }
        if (remove)
            component.Remove(entity);
        ImGui::PopID();
    }

    void PropertiesPanel::DrawAddComponent(Entity entity)
    {
        const auto& components = ComponentRegistry::Get().Entries();
        const auto  addable    = [&](const ComponentEntry& c) {
            return c.Add && !c.Has(entity) && (!c.CanAdd || c.CanAdd(entity));
        };
        const bool  available  = std::any_of(components.begin(), components.end(), addable);
        ImGui::BeginDisabled(!available);
        if (ImGui::Button(ICON_FA_PLUS "  Add Component", {-FLT_MIN, 0.0f}))
            ImGui::OpenPopup("##AddComponent");
        ImGui::EndDisabled();
        ImGui::SetNextWindowSizeConstraints({ImGui::GetItemRectSize().x, 0.0f}, {FLT_MAX, 400.0f});
        if (!ImGui::BeginPopup("##AddComponent"))
            return;
        FilterBox(m_ComponentFilter);
        // Grouped by category, in the order the categories first appear.
        std::vector<std::string> categories;
        for (const auto& c : components)
            if (std::find(categories.begin(), categories.end(), c.Category) == categories.end())
                categories.push_back(c.Category);
        for (const auto& category : categories)
        {
            bool heading = false;
            for (const auto& c : components)
            {
                if (c.Category != category || !addable(c) || !ContainsIgnoreCase(c.Name, m_ComponentFilter.data()))
                    continue;
                if (!heading)
                {
                    ImGui::Separator();
                    ImGui::TextDisabled("%s", category.c_str());
                    heading = true;
                }
                if (ImGui::Selectable((std::string(c.Icon) + "  " + c.Name).c_str()))
                {
                    c.Add(entity);
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::EndPopup();
    }

    // ---------------------------------------------------------------------- prefab parts

    void PropertiesPanel::DrawPrefabPart(Entity entity)
    {
        // A part of a prefab instance: its structure comes from the prefab; transform and
        // material edits are kept as overrides on the instance.
        if (!entity.HasComponent<PrefabOwnedComponent>())
            return;
        Scene*      scene = GetScene();
        const auto& owned = entity.GetComponent<PrefabOwnedComponent>();
        Entity      root  = scene ? scene->GetEntityByUUID(owned.Instance) : Entity {};
        if (!root)
            return;
        ImGui::Spacing();
        ImGui::TextDisabled(ICON_FA_CUBES "  Part of %s", root.GetComponent<TagComponent>().Tag.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Select instance"))
            m_Hierarchy.SetSelected(root);
        const auto& overrides = root.GetComponent<PrefabInstanceComponent>().Overrides;
        if (overrides.count(owned.NodeKey))
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_ROTATE_RIGHT " Revert part"))
                scene->ClearPrefabOverride(root, owned.NodeKey);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Restore this part's transform and materials from the prefab");
        }
    }

    // ---------------------------------------------------------------------- pickers

    bool PropertiesPanel::AssetPicker(const char* label, AssetType type, AssetID& id)
    {
        PropertyGrid::Row(label);
        ImGui::PushID(label);
        bool        changed = false;
        std::string preview = id.IsValid() ? "(missing) " + id.ToString() : "(none)";
        for (const auto& choice : m_AssetChoices)
            if (choice.ID == id)
                preview = choice.Name;
        if (ImGui::BeginCombo("##asset", preview.c_str(), ImGuiComboFlags_HeightLarge))
        {
            FilterBox(m_AssetFilter);
            for (const auto& choice : m_AssetChoices)
            {
                if (choice.Type != type || !ContainsIgnoreCase(choice.Name, m_AssetFilter.data()))
                    continue;
                const bool selected = choice.ID == id;
                if (ImGui::Selectable(choice.Name.c_str(), selected) && !selected)
                {
                    id      = choice.ID;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered() && id.IsValid())
            ImGui::SetTooltip("%s\nDrop a source file here to replace it.", preview.c_str());
        // Accept a source file dragged from the content browser.
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto* payload = ImGui::AcceptDragDropPayload("asset_payload"))
            {
                std::error_code error;
                const auto      dropped = std::filesystem::weakly_canonical(
                    std::filesystem::u8path(std::string(static_cast<const char*>(payload->Data), payload->DataSize)),
                    error);
                for (const auto& choice : m_AssetChoices)
                    if (choice.Type == type && !error && choice.SourcePath == dropped)
                    {
                        changed = choice.ID != id;
                        id      = choice.ID;
                        break;
                    }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
        return changed;
    }

    std::string PropertiesPanel::MaterialName(AssetID material)
    {
        if (!material.IsValid())
            return "(default)";
        std::string name = material.ToString();
        if (m_Assets)
        {
            const auto status = m_Assets->Request<MaterialDataAsset>(material);
            if (status.IsReady())
                name = status.Asset->GetData().Name.empty() ? "(unnamed)" : status.Asset->GetData().Name;
            else if (status.State == AssetLoadState::Loading)
                name = "(loading)";
        }
        for (const auto& choice : m_AssetChoices)
            if (choice.ID == material)
                return name + "  [" + choice.SourcePath.filename().u8string() + "]";
        return name;
    }

    bool PropertiesPanel::MaterialPicker(const char* label, AssetID imported, AssetID current, AssetID& chosen)
    {
        PropertyGrid::Row(label);
        bool       picked     = false;
        const bool overridden = current != imported;
        if (overridden)
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.20f, 0.30f, 0.45f, 1.0f));
        const bool open = ImGui::BeginCombo("##material", MaterialName(current).c_str(), ImGuiComboFlags_HeightLarge);
        if (overridden)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(overridden ? "Replaced; imported: %s" : "Imported material: %s",
                              MaterialName(imported).c_str());
        if (!open)
            return false;
        FilterBox(m_AssetFilter);
        if (ImGui::Selectable(("Imported: " + MaterialName(imported)).c_str(), !overridden))
        {
            chosen = {};
            picked = true;
        }
        ImGui::Separator();
        for (const auto& choice : m_AssetChoices)
        {
            if (choice.Type != AssetType::Material)
                continue;
            const auto name = MaterialName(choice.ID);
            if (!ContainsIgnoreCase(name, m_AssetFilter.data()))
                continue;
            ImGui::PushID(choice.ID.ToString().c_str());
            if (ImGui::Selectable(name.c_str(), choice.ID == current))
            {
                chosen = choice.ID;
                picked = true;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
        return picked;
    }
} // namespace Kans
