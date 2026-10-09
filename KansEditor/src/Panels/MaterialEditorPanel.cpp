#include "MaterialEditorPanel.h"
#include <fstream>
#include <imgui.h>
#include "Kans3D/Asset/Importer/ImporterRegistry.h"
#include "Kans3D/Asset/Storage/ProductFiles.h"

namespace Kans
{
    namespace
    {
        bool LooksLikeColor(const std::string& name)
        {
            std::string lower(name);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            for (const char* word : {"color", "colour", "tint", "albedo"})
                if (lower.find(word) != std::string::npos)
                    return true;
            return false;
        }

        std::optional<AssetError> WriteText(const std::filesystem::path& file, const std::string& text)
        {
            std::ofstream output(file, std::ios::binary | std::ios::trunc);
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.close();
            if (!output)
                return AssetError {AssetErrorCode::IoError, "Cannot write " + file.u8string()};
            return std::nullopt;
        }

        std::filesystem::path Canonical(const std::filesystem::path& path)
        {
            std::error_code error;
            auto            result = std::filesystem::weakly_canonical(path, error);
            return error ? path.lexically_normal() : result;
        }
    } // namespace

    void MaterialEditorPanel::Open(const std::filesystem::path& file)
    {
        m_Open   = true;
        m_Dirty  = false;
        m_File   = file;
        m_Source = {};
        m_Status.clear();
        m_Building = false;
        m_PendingTexture.reset();
        m_Thumbnails.clear();
        auto bytes = ReadProductFile(file, 1024 * 1024);
        if (const auto* error = std::get_if<AssetError>(&bytes))
        {
            m_Status = error->Message;
            return;
        }
        const auto& data   = std::get<std::vector<std::byte>>(bytes);
        auto        parsed = ParseMaterialSource(data.data(), data.size());
        if (const auto* error = std::get_if<AssetError>(&parsed))
            m_Status = error->Message + " (saving replaces the file)";
        else
            m_Source = std::get<MaterialSource>(std::move(parsed));
    }

    void MaterialEditorPanel::Create(const std::filesystem::path& directory)
    {
        std::filesystem::path file;
        for (int index = 0; index < 1000; ++index)
        {
            file = directory / ("NewMaterial" + (index ? std::to_string(index) : std::string()) + ".material");
            std::error_code error;
            if (!std::filesystem::exists(file, error))
                break;
        }
        MaterialSource source;
        source.Name = file.stem().u8string();
        if (auto error = WriteText(file, SerializeMaterialSource(source)))
        {
            m_Open   = true;
            m_Status = error->Message;
            return;
        }
        if (m_Service)
            (void)m_Service->RefreshSources();
        Open(file);
    }

    std::string MaterialEditorPanel::ChoiceName(AssetID id) const
    {
        for (const auto& choice : m_Choices)
            if (choice.ID == id)
                return choice.Name;
        return id.ToString();
    }

    const MaterialSchema* MaterialEditorPanel::Schema()
    {
        if (!m_Source.Shader.IsValid())
        {
            if (!m_Schema || m_SchemaShader)
            {
                m_SchemaShader.reset();
                m_Schema = std::get<MaterialSchema>(DescribeMaterial(nullptr));
            }
            return &*m_Schema;
        }
        if (!m_Assets)
        {
            m_SchemaError = "Assets are not loaded yet.";
            return nullptr;
        }
        auto status = m_Assets->Request<ShaderDataAsset>(m_Source.Shader);
        if (status.Error)
        {
            m_SchemaError = status.Error->Message;
            return nullptr;
        }
        if (!status.IsReady())
        {
            m_SchemaError = "Loading shader...";
            return nullptr;
        }
        if (status.Asset != m_SchemaShader || !m_Schema)
        {
            m_SchemaShader = status.Asset;
            auto described = DescribeMaterial(&status.Asset->GetData());
            if (auto* error = std::get_if<AssetError>(&described))
            {
                m_Schema.reset();
                m_SchemaError = error->Message;
                return nullptr;
            }
            m_Schema = std::get<MaterialSchema>(std::move(described));
        }
        return m_Schema ? &*m_Schema : nullptr;
    }

    void MaterialEditorPanel::DrawParameter(const MaterialParameterInfo& parameter, const MaterialSchema& schema)
    {
        std::vector<double> values;
        if (const auto found = m_Source.Parameters.find(parameter.Name); found != m_Source.Parameters.end())
            values = found->second;
        else if (const auto fallback = schema.Defaults.find(parameter.Name); fallback != schema.Defaults.end())
            values = fallback->second;
        values.resize(parameter.ValueCount(), 0.0);

        using Hint            = ShaderParameterHint::Kind;
        const auto hint       = parameter.Hint ? std::optional<Hint>(parameter.Hint->Type) : std::nullopt;
        const bool toggle     = hint == Hint::Toggle || (!hint && parameter.Type == ShaderUniformType::Boolean);
        const bool floating   = parameter.Type == ShaderUniformType::Float;
        const auto chunks     = parameter.ArrayCount * parameter.Columns;
        const int  width      = static_cast<int>(parameter.Components);
        // Without a hint, color-named vec3/vec4 floats get a color picker.
        const bool color = chunks == 1 && floating && width >= 3 &&
                           (hint == Hint::Color || (!hint && LooksLikeColor(parameter.Name)));

        bool changed = false;
        ImGui::PushID(parameter.Name.c_str());
        for (uint32_t chunk = 0; chunk < chunks; ++chunk)
        {
            std::string label = parameter.Name;
            if (parameter.ArrayCount > 1)
                label += "[" + std::to_string(chunk / parameter.Columns) + "]";
            if (parameter.Columns > 1)
                label += " col " + std::to_string(chunk % parameter.Columns);
            double* slice = values.data() + size_t(chunk) * width;
            ImGui::PushID(static_cast<int>(chunk));
            if (toggle)
            {
                for (int i = 0; i < width; ++i)
                {
                    bool flag = slice[i] != 0.0;
                    ImGui::PushID(i);
                    if (ImGui::Checkbox(i + 1 == width ? label.c_str() : "##flag", &flag))
                    {
                        slice[i] = flag ? 1.0 : 0.0;
                        changed  = true;
                    }
                    ImGui::PopID();
                    if (i + 1 != width)
                        ImGui::SameLine();
                }
            }
            else if (floating)
            {
                float f[4] = {};
                for (int i = 0; i < width; ++i)
                    f[i] = static_cast<float>(slice[i]);
                bool edited = false;
                if (color)
                    edited = width == 4 ? ImGui::ColorEdit4(label.c_str(), f, ImGuiColorEditFlags_Float) :
                                          ImGui::ColorEdit3(label.c_str(), f, ImGuiColorEditFlags_Float);
                else if (hint == Hint::Range)
                {
                    const float min = float(parameter.Hint->Min), max = float(parameter.Hint->Max);
                    edited = ImGui::SliderScalarN(label.c_str(), ImGuiDataType_Float, f, width, &min, &max);
                }
                else
                    edited = ImGui::DragScalarN(label.c_str(), ImGuiDataType_Float, f, width, 0.01f);
                if (edited)
                    for (int i = 0; i < width; ++i)
                        slice[i] = f[i];
                changed |= edited;
            }
            else
            {
                const bool signedValue = parameter.Type == ShaderUniformType::Signed;
                int32_t    s[4]        = {};
                uint32_t   u[4]        = {};
                for (int i = 0; i < width; ++i)
                {
                    s[i] = static_cast<int32_t>(slice[i]);
                    u[i] = static_cast<uint32_t>(std::max(0.0, slice[i]));
                }
                void*            data = signedValue ? static_cast<void*>(s) : static_cast<void*>(u);
                const ImGuiDataType type = signedValue ? ImGuiDataType_S32 : ImGuiDataType_U32;
                bool                edited;
                if (hint == Hint::Range)
                {
                    const int32_t  smin = int32_t(parameter.Hint->Min), smax = int32_t(parameter.Hint->Max);
                    const uint32_t umin = uint32_t(std::max(0.0, parameter.Hint->Min)),
                                   umax = uint32_t(std::max(0.0, parameter.Hint->Max));
                    edited = ImGui::SliderScalarN(label.c_str(),
                                                  type,
                                                  data,
                                                  width,
                                                  signedValue ? static_cast<const void*>(&smin) : &umin,
                                                  signedValue ? static_cast<const void*>(&smax) : &umax);
                }
                else
                    edited = ImGui::DragScalarN(label.c_str(), type, data, width, 0.1f);
                if (edited)
                    for (int i = 0; i < width; ++i)
                        slice[i] = signedValue ? double(s[i]) : double(u[i]);
                changed |= edited;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
        if (changed)
        {
            m_Source.Parameters[parameter.Name] = std::move(values);
            m_Dirty                             = true;
        }
    }

    void MaterialEditorPanel::SetTexture(const std::string& slot, AssetID texture)
    {
        auto found = m_Source.Textures.find(slot);
        if (texture.IsValid() ? found != m_Source.Textures.end() && found->second == texture :
                                found == m_Source.Textures.end())
            return;
        if (texture.IsValid())
            m_Source.Textures[slot] = texture;
        else
            m_Source.Textures.erase(slot);
        m_Dirty = true;
    }

    void MaterialEditorPanel::DrawThumbnail(AssetID texture, float size)
    {
        Thumbnail* thumbnail = nullptr;
        if (texture.IsValid() && m_Assets)
        {
            auto  status = m_Assets->Request<TextureAsset>(texture);
            auto& entry  = m_Thumbnails[texture];
            if (status.IsReady() && status.Asset != entry.Source)
            {
                entry.Source  = status.Asset;
                auto uploaded = Texture2D::CreateFromProduct(status.Asset->GetData());
                entry.Texture =
                    std::holds_alternative<Ref<Texture2D>>(uploaded) ? std::get<Ref<Texture2D>>(uploaded) : nullptr;
            }
            if (entry.Texture)
                thumbnail = &entry;
        }
        if (!thumbnail)
        {
            ImGui::Button(texture.IsValid() ? "..." : "None", {size, size});
            return;
        }
        // Uploads store the image bottom row first (OpenGL convention).
        const ImVec2 uv0(0, 1), uv1(1, 0);
        const auto   id = (ImTextureID)(uintptr_t)thumbnail->Texture->GetRenererID();
        ImGui::Image(id, {size, size}, uv0, uv1);
        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            const float width  = float(thumbnail->Texture->GetWidth());
            const float height = float(thumbnail->Texture->GetHeight());
            const float scale  = std::min(256.0f / std::max(width, height), 1.0f);
            ImGui::Image(id, {width * scale, height * scale}, uv0, uv1);
            ImGui::Text("%s", ChoiceName(texture).c_str());
            ImGui::TextDisabled("%u x %u", thumbnail->Texture->GetWidth(), thumbnail->Texture->GetHeight());
            ImGui::EndTooltip();
        }
    }

    void MaterialEditorPanel::DrawTexture(const std::string& name)
    {
        constexpr float Size    = 48.0f;
        const auto      found   = m_Source.Textures.find(name);
        const AssetID   current = found == m_Source.Textures.end() ? AssetID {} : found->second;

        ImGui::PushID(name.c_str());
        ImGui::BeginGroup();
        DrawThumbnail(current, Size);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(name.c_str());
        const bool pending = m_PendingTexture && m_PendingTexture->first == name;
        const std::string preview =
            pending ? "Importing..." : current.IsValid() ? ChoiceName(current) : std::string("(none)");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##texture", preview.c_str(), ImGuiComboFlags_HeightLarge))
        {
            if (ImGui::IsWindowAppearing())
            {
                m_TextureFilter.clear();
                ImGui::SetKeyboardFocusHere();
            }
            char filter[256] = {};
            m_TextureFilter.copy(filter, sizeof(filter) - 1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##filter", "Search", filter, sizeof(filter)))
                m_TextureFilter = filter;
            std::string needle(m_TextureFilter);
            std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            std::vector<const EditorAssetChoice*> matches;
            for (const auto& choice : m_Choices)
            {
                if (choice.Type != AssetType::Texture)
                    continue;
                std::string haystack(choice.Name);
                std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (haystack.find(needle) != std::string::npos)
                    matches.push_back(&choice);
            }
            if (ImGui::Selectable("(none)", !current.IsValid()))
                SetTexture(name, {});
            // Only visible rows load thumbnails.
            constexpr float  Row = 28.0f;
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(matches.size()), Row + ImGui::GetStyle().ItemSpacing.y);
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const auto& choice = *matches[i];
                    ImGui::PushID(i);
                    const float start = ImGui::GetCursorPosX();
                    if (ImGui::Selectable(
                            "##choice", choice.ID == current, ImGuiSelectableFlags_AllowItemOverlap, {0, Row}))
                        SetTexture(name, choice.ID);
                    // Thumbnail and name are drawn over the selectable row.
                    ImGui::SameLine(start);
                    DrawThumbnail(choice.ID, Row);
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(choice.Name.c_str());
                    ImGui::PopID();
                }
            if (matches.empty())
                ImGui::TextDisabled("No imported textures match.");
            ImGui::EndCombo();
        }
        ImGui::EndGroup();
        ImGui::EndGroup();

        // Drop an image from the content browser on the row.
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto* payload = ImGui::AcceptDragDropPayload("asset_payload"))
                AssignDroppedFile(name,
                                  std::filesystem::u8path(std::string(
                                      static_cast<const char*>(payload->Data), size_t(payload->DataSize))));
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();
    }

    void MaterialEditorPanel::AssignDroppedFile(const std::string& slot, const std::filesystem::path& file)
    {
        const auto dropped = Canonical(file);
        // Already imported: use its Texture Product.
        for (const auto& choice : m_Choices)
            if (choice.Type == AssetType::Texture && choice.SourcePath == dropped)
            {
                SetTexture(slot, choice.ID);
                return;
            }
        if (!m_Service)
            return;
        const auto findSource = [&]() -> std::optional<SourceAssetMetadata> {
            auto sources = m_Service->GetSources();
            if (!std::holds_alternative<std::vector<SourceAssetMetadata>>(sources))
                return std::nullopt;
            const auto& root = m_Service->GetConfig().AssetDirectory;
            for (const auto& source : std::get<std::vector<SourceAssetMetadata>>(sources))
                if (source.Exists && Canonical(root / source.Path.GetPath()) == dropped)
                    return source;
            return std::nullopt;
        };
        auto source = findSource();
        if (!source)
        {
            (void)m_Service->RefreshSources();
            source = findSource();
        }
        if (!source)
        {
            m_Status = file.filename().u8string() + " is not a project asset.";
            return;
        }
        const auto registry = CreateDefaultImporterRegistry();
        const auto importer = std::get<ImporterRegistry>(registry).Resolve(file, source->Import.Importer);
        if (std::holds_alternative<AssetError>(importer) ||
            std::get<RegisteredImporter>(importer).Name != "TextureImporter")
        {
            m_Status = file.filename().u8string() + " is not a texture.";
            return;
        }
        AssetBuildOptions options;
        options.Target = EditorAssetTarget;
        auto started   = m_Service->Start(source->sourceAssetID, options);
        if (const auto* error = std::get_if<AssetError>(&started))
        {
            m_Status = "Cannot import " + file.filename().u8string() + ": " + error->Message;
            return;
        }
        m_PendingTexture = std::make_pair(slot, source->sourceAssetID);
        m_Status         = "Importing " + file.filename().u8string() + "...";
    }

    void MaterialEditorPanel::FinishPendingTexture()
    {
        if (!m_PendingTexture || !m_Service)
            return;
        auto snapshot = m_Service->GetSnapshot();
        const auto* status = std::get_if<EditorAssetImportSnapshot>(&snapshot);
        if (!status || status->State == EditorAssetImportState::Building)
            return;
        const auto [slot, source] = *m_PendingTexture;
        // The editor refreshes the asset choices when the build completes.
        for (const auto& choice : m_Choices)
            if (choice.Type == AssetType::Texture && choice.ID.GetSourceID() == source)
            {
                SetTexture(slot, choice.ID);
                m_PendingTexture.reset();
                m_Status = "Texture imported.";
                return;
            }
        if (status->Source == source && status->State != EditorAssetImportState::Succeeded)
        {
            m_PendingTexture.reset();
            m_Status = status->Error ? "Texture import failed: " + status->Error->Message :
                                       std::string("Texture import did not finish.");
        }
    }

    void MaterialEditorPanel::Save()
    {
        // Values the current Shader does not declare would fail the import.
        if (const auto* schema = Schema())
        {
            for (auto it = m_Source.Parameters.begin(); it != m_Source.Parameters.end();)
                it = std::any_of(schema->Parameters.begin(),
                                 schema->Parameters.end(),
                                 [&](const auto& p) { return p.Name == it->first; }) ?
                         std::next(it) :
                         m_Source.Parameters.erase(it);
            for (auto it = m_Source.Textures.begin(); it != m_Source.Textures.end();)
                it = std::find(schema->Textures.begin(), schema->Textures.end(), it->first) != schema->Textures.end() ?
                         std::next(it) :
                         m_Source.Textures.erase(it);
        }
        if (auto error = WriteText(m_File, SerializeMaterialSource(m_Source)))
        {
            m_Status = error->Message;
            return;
        }
        m_Dirty = false;
        if (!m_Service)
            return;
        const auto findSource = [&]() -> std::optional<SourceAssetID> {
            auto sources = m_Service->GetSources();
            if (!std::holds_alternative<std::vector<SourceAssetMetadata>>(sources))
                return std::nullopt;
            const auto  file = Canonical(m_File);
            const auto& root = m_Service->GetConfig().AssetDirectory;
            for (const auto& source : std::get<std::vector<SourceAssetMetadata>>(sources))
                if (source.Exists && Canonical(root / source.Path.GetPath()) == file)
                    return source.sourceAssetID;
            return std::nullopt;
        };
        auto source = findSource();
        if (!source)
        {
            (void)m_Service->RefreshSources();
            source = findSource();
        }
        if (!source)
        {
            m_Status = "Saved, but the file is not a project asset.";
            return;
        }
        AssetBuildOptions options;
        options.Target = EditorAssetTarget;
        auto started   = m_Service->Start(*source, options);
        if (const auto* error = std::get_if<AssetError>(&started))
            m_Status = "Saved; build not started: " + error->Message;
        else
        {
            m_Status   = "Building...";
            m_Building = true;
        }
    }

    void MaterialEditorPanel::Draw()
    {
        if (!m_Open)
            return;
        if (!ImGui::Begin("Material Editor", &m_Open))
        {
            ImGui::End();
            return;
        }
        ImGui::TextUnformatted(m_File.filename().u8string().c_str());
        if (m_Dirty)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(modified)");
        }

        char name[256] = {};
        m_Source.Name.copy(name, sizeof(name) - 1);
        if (ImGui::InputText("Name", name, sizeof(name)))
        {
            m_Source.Name = name;
            m_Dirty       = true;
        }

        const std::string shaderName = m_Source.Shader.IsValid() ? ChoiceName(m_Source.Shader) : "Engine PBR";
        if (ImGui::BeginCombo("Shader", shaderName.c_str()))
        {
            if (ImGui::Selectable("Engine PBR", !m_Source.Shader.IsValid()))
            {
                m_Source.Shader = {};
                m_Dirty         = true;
            }
            for (const auto& choice : m_Choices)
                if (choice.Type == AssetType::Shader &&
                    ImGui::Selectable(choice.Name.c_str(), choice.ID == m_Source.Shader))
                {
                    m_Source.Shader = choice.ID;
                    m_Dirty         = true;
                }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shaders appear here once imported. Import a .shaderprog (vertex + fragment)\n"
                              "that follows the material contract, e.g. assets/shaders/Toon/Toon.shaderprog.");

        FinishPendingTexture();
        ImGui::Separator();
        if (const auto* schema = Schema())
        {
            if (schema->Parameters.empty() && schema->Textures.empty())
                ImGui::TextDisabled("This shader has no material parameters.");
            for (const auto& parameter : schema->Parameters)
                DrawParameter(parameter, *schema);
            if (!schema->Textures.empty())
            {
                ImGui::Spacing();
                ImGui::TextDisabled("Textures");
                for (const auto& texture : schema->Textures)
                    DrawTexture(texture);
            }
        }
        else
            ImGui::TextWrapped("%s", m_SchemaError.c_str());

        ImGui::Separator();
        if (ImGui::Button("Save"))
            Save();
        ImGui::SameLine();
        if (ImGui::Button("Revert"))
            Open(m_File);

        if (m_Building && m_Service)
        {
            auto snapshot = m_Service->GetSnapshot();
            if (const auto* status = std::get_if<EditorAssetImportSnapshot>(&snapshot);
                status && status->State != EditorAssetImportState::Building)
            {
                m_Building = false;
                if (status->State == EditorAssetImportState::Succeeded)
                    m_Status = "Saved and imported.";
                else
                    m_Status = status->Error ? status->Error->Message : std::string("Import did not finish.");
            }
        }
        if (!m_Status.empty())
            ImGui::TextWrapped("%s", m_Status.c_str());
        ImGui::End();
    }
} // namespace Kans
