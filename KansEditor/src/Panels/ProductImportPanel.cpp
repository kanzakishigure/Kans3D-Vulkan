#include "ProductImportPanel.h"
#include <imgui.h>
#include <algorithm>
#include <misc/cpp/imgui_stdlib.h>

namespace Kans
{
    void ProductImportPanel::Bind(EditorAssetImportService* service)
    {
        m_Service = service;
        m_Sources.clear();
        m_DeletionPlan.reset();
        m_Selected = {};
        m_Error.clear();
        if (service)
            ReloadSources();
    }

    void ProductImportPanel::Record(const std::variant<std::monostate, AssetError>& result)
    {
        m_Error.clear();
        if (const auto* error = std::get_if<AssetError>(&result))
            m_Error = error->Message;
    }

    void ProductImportPanel::ReloadSources()
    {
        m_DeletionPlan.reset();
        auto result = m_Service->GetSources();
        if (const auto* error = std::get_if<AssetError>(&result))
        {
            m_Error = error->Message;
            return;
        }
        m_Sources = std::get<std::vector<SourceAssetMetadata>>(std::move(result));
        std::sort(m_Sources.begin(), m_Sources.end(), [](const auto& a, const auto& b) {
            return a.Path.ToString() < b.Path.ToString();
        });
        for (const auto& source : m_Sources)
            if (source.sourceAssetID == m_Selected)
            {
                Select(source);
                return;
            }
        m_Selected      = {};
        m_Configuration = {};
    }

    void ProductImportPanel::Select(const SourceAssetMetadata& source)
    {
        m_DeletionPlan.reset();
        m_Selected      = source.sourceAssetID;
        m_Configuration = source.Import;
        m_NewKey.clear();
        m_NewValue.clear();
        m_MoveDestination = source.Path.ToString();
        m_SlotsStale      = true;
        m_ExtractReport.clear();
        // Extracted materials go next to the model by default.
        m_ExtractDirectory = (source.Path.GetPath().parent_path() / "Materials").generic_u8string();
    }

    void ProductImportPanel::LoadSlots()
    {
        m_SlotsStale = false;
        m_Slots.reset();
        m_SlotsError.clear();
        if (!m_Selected.IsValid())
            return;
        // Extraction registers new .material sources; list them without changing the selection.
        if (auto sources = m_Service->GetSources(); std::holds_alternative<std::vector<SourceAssetMetadata>>(sources))
        {
            m_Sources = std::get<std::vector<SourceAssetMetadata>>(std::move(sources));
            std::sort(m_Sources.begin(), m_Sources.end(), [](const auto& a, const auto& b) {
                return a.Path.ToString() < b.Path.ToString();
            });
        }
        auto slots = m_Service->GetModelMaterialSlots(m_Selected);
        if (const auto* error = std::get_if<AssetError>(&slots))
            m_SlotsError = error->Message;
        else
            m_Slots = std::get<std::vector<ModelMaterialSlot>>(std::move(slots));
    }

    void ProductImportPanel::Rebuild()
    {
        AssetBuildOptions options;
        options.Target = EditorAssetTarget;
        Record(m_Service->Start(m_Selected, std::move(options)));
    }

    void ProductImportPanel::DrawModelMaterials(bool busy)
    {
        // Slots change after builds and selection changes; never query while building.
        if (m_WasBusy && !busy)
            m_SlotsStale = true;
        m_WasBusy = busy;
        if (!busy && m_SlotsStale)
            LoadSlots();
        if (!m_Slots)
            return; // Not a model, or not imported yet (the reason is in m_SlotsError).
        if (!ImGui::TreeNodeEx("Materials", ImGuiTreeNodeFlags_DefaultOpen))
            return;
        if (m_Slots->empty())
            ImGui::TextDisabled("This model has no materials.");

        // Project materials a slot can use: published Material Products of .material sources.
        std::vector<std::pair<AssetID, std::string>> choices;
        const auto                                   registry = m_Service->GetRuntimeAssetRegistry();
        const auto&                                  products = registry->GetAllProducts();
        for (const auto& source : m_Sources)
        {
            if (source.Missing || source.Path.GetPath().extension() != ".material")
                continue;
            for (const auto& product : products)
                if (product.Metadata.assetID.GetSourceID() == source.sourceAssetID &&
                    product.Metadata.Type == AssetType::Material && product.Target == EditorAssetTarget)
                    choices.emplace_back(product.Metadata.assetID, source.Path.GetPath().filename().u8string());
        }
        const auto nameOf = [&](AssetID id) {
            for (const auto& [choice, name] : choices)
                if (choice == id)
                    return name;
            // A remap to a source that is not published (yet): name it by its file.
            for (const auto& source : m_Sources)
                if (source.sourceAssetID == id.GetSourceID())
                    return source.Path.GetPath().filename().u8string() + (source.Missing ? " (missing)" : " (not built)");
            return id.ToString() + " (unavailable)";
        };

        if (ImGui::BeginTable("##Slots", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("##Extract", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();
            for (const auto& slot : *m_Slots)
            {
                ImGui::PushID(slot.Key.c_str());
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(slot.DisplayName.c_str());
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::BeginDisabled(busy);
                const std::string current = slot.Remap ? nameOf(*slot.Remap) : "Embedded";
                if (ImGui::BeginCombo("##material", current.c_str(), ImGuiComboFlags_HeightLarge))
                {
                    std::optional<std::optional<AssetID>> picked;
                    if (ImGui::Selectable("Embedded", !slot.Remap))
                        picked = std::optional<AssetID> {};
                    if (!choices.empty())
                        ImGui::Separator();
                    for (const auto& [id, name] : choices)
                        if (ImGui::Selectable(name.c_str(), slot.Remap == id))
                            picked = id;
                    ImGui::EndCombo();
                    if (picked && *picked != slot.Remap)
                    {
                        Record(m_Service->SetModelMaterialRemap(m_Selected, slot.Key, *picked));
                        if (m_Error.empty())
                            Rebuild();
                        m_SlotsStale = true;
                    }
                }
                if (ImGui::IsItemHovered())
                {
                    std::string path;
                    for (const auto& source : m_Sources)
                        if (slot.Remap && source.sourceAssetID == slot.Remap->GetSourceID())
                            path = source.Path.ToString() + "\n";
                    ImGui::SetTooltip("%s%s",
                                      path.c_str(),
                                      slot.Remap ? "Every use of this model draws this material." :
                                                   "The material imported with the model (read-only).");
                }
                ImGui::TableNextColumn();
                ImGui::BeginDisabled(slot.Remap.has_value());
                const bool extract = ImGui::SmallButton("Extract");
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(slot.Remap ? "Already uses a project material." :
                                                   "Write this material to an editable .material file and use it.");
                ImGui::EndDisabled();
                ImGui::PopID();
                if (extract)
                {
                    m_ExtractReport.clear();
                    const auto key    = slot.Key;
                    auto       result = m_Service->ExtractModelMaterials(
                        m_Selected, std::filesystem::u8path(m_ExtractDirectory), {key});
                    if (const auto* error = std::get_if<AssetError>(&result))
                        m_Error = error->Message;
                    else
                    {
                        const auto& value = std::get<ModelMaterialExtractionResult>(result);
                        for (const auto& created : value.Created)
                            m_ExtractReport.push_back("Created " + created.File.filename().u8string());
                        m_Error = value.Error ? value.Error->Message : std::string();
                        if (value.RemapsApplied && !value.Created.empty())
                            Rebuild();
                    }
                    m_SlotsStale = true;
                    break; // The slot list is reloaded next frame.
                }
            }
            ImGui::EndTable();
        }

        ImGui::BeginDisabled(busy);
        ImGui::SetNextItemWidth(-120.0f);
        ImGui::InputText("##ExtractDirectory", &m_ExtractDirectory);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Folder for extracted materials, relative to the asset directory.");
        ImGui::SameLine();
        const bool remaining =
            std::any_of(m_Slots->begin(), m_Slots->end(), [](const auto& slot) { return !slot.Remap; });
        ImGui::BeginDisabled(!remaining);
        if (ImGui::Button("Extract all", {-FLT_MIN, 0.0f}))
        {
            m_ExtractReport.clear();
            auto result = m_Service->ExtractModelMaterials(m_Selected, std::filesystem::u8path(m_ExtractDirectory));
            if (const auto* error = std::get_if<AssetError>(&result))
                m_Error = error->Message;
            else
            {
                const auto& value = std::get<ModelMaterialExtractionResult>(result);
                for (const auto& created : value.Created)
                    m_ExtractReport.push_back("Created " + created.File.filename().u8string());
                if (!value.Skipped.empty())
                    m_ExtractReport.push_back(std::to_string(value.Skipped.size()) +
                                              " slot(s) already use project materials");
                m_Error = value.Error ? value.Error->Message : std::string();
                if (value.RemapsApplied && !value.Created.empty())
                    Rebuild();
            }
            m_SlotsStale = true;
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        for (const auto& line : m_ExtractReport)
            ImGui::TextDisabled("%s", line.c_str());
        ImGui::TreePop();
    }

    void ProductImportPanel::OpenWithFile(const std::filesystem::path& path)
    {
        if (!m_Service)
            return;
        std::error_code error;
        const auto      absolute = std::filesystem::absolute(path, error).lexically_normal();
        if (error)
        {
            m_Error = error.message();
            return;
        }
        ReloadSources();
        for (const auto& source : m_Sources)
        {
            const auto full = m_Service->GetConfig().AssetDirectory / source.Path.GetPath();
            error.clear();
            if (source.Exists && !source.Missing && std::filesystem::equivalent(full, absolute, error) && !error)
            {
                Select(source);
                m_Error.clear();
                return;
            }
        }
        m_Error = "File is not a registered source. Place it under the project asset directory and scan sources.";
    }

    namespace
    {
        const char* StateName(EditorAssetImportState state)
        {
            switch (state)
            {
                case EditorAssetImportState::Idle:
                    return "Idle";
                case EditorAssetImportState::Building:
                    return "Building";
                case EditorAssetImportState::Succeeded:
                    return "Succeeded";
                case EditorAssetImportState::Failed:
                    return "Failed";
                case EditorAssetImportState::Cancelled:
                    return "Cancelled";
            }
            return "Unknown";
        }
        const char* PhaseName(PublicationPhase phase)
        {
            switch (phase)
            {
                case PublicationPhase::Validated:
                    return "Validated staged set";
                case PublicationPhase::ProductsWritten:
                    return "Products written";
                case PublicationPhase::BeforeCommit:
                    return "Before catalog commit";
                case PublicationPhase::Committed:
                    return "Catalog committed";
            }
            return "Unknown";
        }
    } // namespace

    void ProductImportPanel::Draw()
    {
        if (!m_Service)
        {
            ImGui::TextUnformatted("Project is unavailable.");
            return;
        }
        auto result = m_Service->GetSnapshot();
        if (const auto* error = std::get_if<AssetError>(&result))
        {
            ImGui::TextWrapped("%s", error->Message.c_str());
            return;
        }
        const auto& status = std::get<EditorAssetImportSnapshot>(result);
        const bool  busy   = status.State == EditorAssetImportState::Building;
        ImGui::Text("Status: %s", StateName(status.State));
        if (busy)
        {
            if (status.Phase)
                ImGui::Text("Last publication checkpoint: %s", PhaseName(*status.Phase));
            else
                ImGui::TextUnformatted("Discovering dependencies / parsing sources");
            if (status.CancelRequested)
                ImGui::TextUnformatted("Cancellation requested; waiting for a safe checkpoint.");
            else if (ImGui::Button("Cancel Import"))
                Record(m_Service->Cancel());
        }
        if (status.Error)
            ImGui::TextWrapped("%s", status.Error->Message.c_str());
        if (status.Result)
        {
            const auto& root = status.Result->Root;
            ImGui::TextUnformatted(root.Disposition == AssetBuildDisposition::UpToDate ? "Root is up to date." :
                                                                                         "Root published.");
            ImGui::Text("Dependencies built: %zu", status.Result->Dependencies.size());
            for (const auto* build : {&root})
                for (const auto& warning : build->Warnings)
                    ImGui::TextWrapped("Warning: %s", warning.c_str());
            for (const auto& dependency : status.Result->Dependencies)
                for (const auto& warning : dependency.Warnings)
                    ImGui::TextWrapped("Warning: %s", warning.c_str());
            if (root.Registry && ImGui::TreeNode("Published root Products"))
            {
                for (const auto& product : root.Registry->GetAllProducts())
                    if (product.Metadata.assetID.GetSourceID() == status.Source && product.Target == status.Target)
                    {
                        const auto id = product.Metadata.assetID.ToString();
                        ImGui::TextUnformatted(id.c_str());
                        ImGui::SameLine();
                        ImGui::PushID(id.c_str());
                        if (ImGui::SmallButton("Copy ID"))
                            ImGui::SetClipboardText(id.c_str());
                        ImGui::PopID();
                    }
                ImGui::TreePop();
            }
        }
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Scan Sources"))
        {
            Record(m_Service->RefreshSources());
            if (m_Error.empty())
                ReloadSources();
        }
        const auto selected = std::find_if(m_Sources.begin(), m_Sources.end(), [this](const auto& source) {
            return source.sourceAssetID == m_Selected;
        });
        const auto label    = selected == m_Sources.end() ? std::string("Choose source") : selected->Path.ToString();
        if (ImGui::BeginCombo("Source", label.c_str()))
        {
            for (const auto& source : m_Sources)
            {
                const auto id      = source.sourceAssetID.ToString();
                const auto display = source.Path.ToString() + (source.Missing ? " [Missing]" : "");
                ImGui::PushID(id.c_str());
                if (ImGui::Selectable(display.c_str(), source.sourceAssetID == m_Selected))
                    Select(source);
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Force rebuild", &m_Force);
        ImGui::EndDisabled();
        DrawModelMaterials(busy);
        ImGui::BeginDisabled(busy);
        if (m_Selected.IsValid() && ImGui::TreeNode("Source import configuration"))
        {
            ImGui::InputText("Importer (empty = default)", &m_Configuration.Importer);
            for (auto it = m_Configuration.Settings.begin(); it != m_Configuration.Settings.end();)
            {
                ImGui::PushID(it->first.c_str());
                ImGui::TextUnformatted(it->first.c_str());
                // Preserve persisted binary/escaped values; the single-line UI
                // cannot round-trip embedded NUL. Do not silently truncate them.
                if (it->second.find('\0') != std::string::npos)
                    ImGui::TextUnformatted("Contains embedded NUL; edit the sidecar explicitly.");
                else
                    ImGui::InputText("Value", &it->second);
                const bool remove = ImGui::SmallButton("Remove setting");
                ImGui::PopID();
                if (remove)
                    it = m_Configuration.Settings.erase(it);
                else
                    ++it;
            }
            ImGui::InputText("New key", &m_NewKey);
            ImGui::InputText("New value", &m_NewValue);
            if (ImGui::Button("Add setting"))
            {
                auto candidate = m_Configuration;
                if (!candidate.Settings.emplace(m_NewKey, m_NewValue).second || !candidate.IsValid())
                    m_Error = "Invalid or duplicate setting; existing values were preserved.";
                else
                {
                    m_Configuration.Swap(candidate);
                    m_NewKey.clear();
                    m_NewValue.clear();
                    m_Error.clear();
                }
            }
            if (ImGui::Button("Save Configuration"))
            {
                Record(m_Service->SetSourceImportConfiguration(m_Selected, m_Configuration));
                if (m_Error.empty())
                    ReloadSources();
            }
            ImGui::TextWrapped("Save configuration before importing. Builds use persisted .kmeta settings.");
            ImGui::TreePop();
        }
        if (m_Selected.IsValid() && ImGui::TreeNode("Move / Rename Source"))
        {
            ImGui::InputText("Asset-relative destination", &m_MoveDestination);
            ImGui::TextWrapped("The destination directory must exist. Moves keep the asset's identity, settings and "
                               "published Products. Paths inside other source files are not rewritten.");
            if (ImGui::Button("Move source and sidecar"))
            {
                try
                {
                    if (m_MoveDestination.size() > 4096)
                        m_Error = "Destination path exceeds 4096 bytes.";
                    else
                    {
                        Record(m_Service->MoveSourceAsset(m_Selected,
                                                          AssetPath(std::filesystem::u8path(m_MoveDestination))));
                        if (m_Error.empty())
                            ReloadSources();
                    }
                }
                catch (const std::exception& error)
                {
                    m_Error = error.what();
                }
            }
            ImGui::TreePop();
        }
        DrawDeletion();
        ImGui::BeginDisabled(!m_Selected.IsValid());
        if (ImGui::Button("Import / Reimport"))
        {
            AssetBuildOptions options;
            options.Target = EditorAssetTarget;
            options.Force  = m_Force;
            Record(m_Service->Start(m_Selected, std::move(options)));
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (!busy)
        {
            const auto warnings = m_Service->GetProjectWarnings();
            if (const auto* values = std::get_if<std::vector<AssetError>>(&warnings))
                for (const auto& warning : *values)
                    ImGui::TextWrapped("Project warning: %s", warning.Message.c_str());
        }
        if (!m_Error.empty())
            ImGui::TextWrapped("%s", m_Error.c_str());
    }

    void ProductImportPanel::DrawDeletion()
    {
        bool review = false;
        if (m_Selected.IsValid() && ImGui::TreeNode("Delete Asset"))
        {
            ImGui::TextWrapped("Deletes the source file, its sidecar and all of its published Products.");
            if (ImGui::Button("Review deletion..."))
            {
                auto planned = m_Service->PlanSourceAssetDeletion(m_Selected);
                if (const auto* error = std::get_if<AssetError>(&planned))
                    m_Error = error->Message;
                else
                {
                    m_DeletionPlan = std::get<SourceAssetDeletionPlan>(std::move(planned));
                    m_Error.clear();
                    review = true;
                }
            }
            ImGui::TreePop();
        }
        if (review)
            ImGui::OpenPopup("Confirm Asset Deletion");
        if (!ImGui::BeginPopupModal("Confirm Asset Deletion", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return;
        if (m_DeletionPlan)
        {
            const auto& plan = *m_DeletionPlan;
            ImGui::TextWrapped("Source: %s", plan.Source.Path.ToString().c_str());
            if (!plan.Dependents.empty())
            {
                ImGui::TextWrapped("These assets use it and will show it as missing until reimported:");
                for (const auto& dependent : plan.Dependents)
                    ImGui::BulletText("%s", dependent.Path.ToString().c_str());
            }
            ImGui::TextWrapped("Scene references to its Products become unresolved.");
            if (ImGui::Button("Delete asset and files"))
            {
                Record(m_Service->DeleteSourceAsset(plan.Source.sourceAssetID));
                if (m_Error.empty())
                {
                    m_DeletionPlan.reset();
                    ReloadSources();
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        else
            ImGui::TextUnformatted("The source selection changed. Close and review deletion again.");
        if (!m_Error.empty())
            ImGui::TextWrapped("%s", m_Error.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Close"))
        {
            m_DeletionPlan.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
} // namespace Kans
