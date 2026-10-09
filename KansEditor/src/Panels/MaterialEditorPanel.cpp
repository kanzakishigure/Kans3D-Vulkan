#include "MaterialEditorPanel.h"
#include <fstream>
#include <imgui.h>
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

        bool       changed = false;
        const auto chunks  = parameter.ArrayCount * parameter.Columns;
        const int  width   = static_cast<int>(parameter.Components);
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
            switch (parameter.Type)
            {
                case ShaderUniformType::Float:
                {
                    float f[4] = {};
                    for (int i = 0; i < width; ++i)
                        f[i] = static_cast<float>(slice[i]);
                    bool edited = false;
                    if (chunks == 1 && width == 4 && LooksLikeColor(parameter.Name))
                        edited = ImGui::ColorEdit4(label.c_str(), f, ImGuiColorEditFlags_Float);
                    else if (chunks == 1 && width == 3 && LooksLikeColor(parameter.Name))
                        edited = ImGui::ColorEdit3(label.c_str(), f, ImGuiColorEditFlags_Float);
                    else
                        edited = ImGui::DragScalarN(label.c_str(), ImGuiDataType_Float, f, width, 0.01f);
                    if (edited)
                        for (int i = 0; i < width; ++i)
                            slice[i] = f[i];
                    changed |= edited;
                    break;
                }
                case ShaderUniformType::Signed:
                case ShaderUniformType::Unsigned:
                {
                    const bool signedValue = parameter.Type == ShaderUniformType::Signed;
                    int64_t    v[4]        = {};
                    for (int i = 0; i < width; ++i)
                        v[i] = static_cast<int64_t>(slice[i]);
                    int32_t  s[4] = {};
                    uint32_t u[4] = {};
                    for (int i = 0; i < width; ++i)
                    {
                        s[i] = static_cast<int32_t>(v[i]);
                        u[i] = static_cast<uint32_t>(v[i]);
                    }
                    const bool edited = signedValue ?
                                            ImGui::DragScalarN(label.c_str(), ImGuiDataType_S32, s, width, 0.1f) :
                                            ImGui::DragScalarN(label.c_str(), ImGuiDataType_U32, u, width, 0.1f);
                    if (edited)
                        for (int i = 0; i < width; ++i)
                            slice[i] = signedValue ? double(s[i]) : double(u[i]);
                    changed |= edited;
                    break;
                }
                case ShaderUniformType::Boolean:
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
                    break;
                default:
                    break;
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

    void MaterialEditorPanel::DrawTexture(const std::string& name)
    {
        const auto  found   = m_Source.Textures.find(name);
        std::string preview = found == m_Source.Textures.end() ? "(none)" : ChoiceName(found->second);
        if (!ImGui::BeginCombo(name.c_str(), preview.c_str()))
            return;
        if (ImGui::Selectable("(none)", found == m_Source.Textures.end()))
        {
            m_Source.Textures.erase(name);
            m_Dirty = true;
        }
        for (const auto& choice : m_Choices)
        {
            if (choice.Type != AssetType::Texture)
                continue;
            const bool selected = found != m_Source.Textures.end() && found->second == choice.ID;
            if (ImGui::Selectable(choice.Name.c_str(), selected))
            {
                m_Source.Textures[name] = choice.ID;
                m_Dirty                 = true;
            }
        }
        ImGui::EndCombo();
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
