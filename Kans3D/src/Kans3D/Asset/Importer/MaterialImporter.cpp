#include "kspch.h"
#include "MaterialImporter.h"
#include <set>
#include <stdexcept>
#include <yaml-cpp/yaml.h>
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/Asset/Runtime/MaterialBindings.h"
#include "Kans3D/Asset/Storage/ProductFiles.h"

namespace Kans
{
    namespace
    {
        void Fields(const YAML::Node& node, std::initializer_list<const char*> fields)
        {
            if (!node.IsMap() || node.size() != fields.size())
                throw std::runtime_error("Invalid Material source fields");
            std::set<std::string> expected;
            for (auto field : fields)
                expected.insert(field);
            for (const auto& entry : node)
                if (!entry.first.IsScalar() || !expected.erase(entry.first.as<std::string>()))
                    throw std::runtime_error("Unknown or duplicate Material source field");
        }
        size_t Sequence(const YAML::Node& node, size_t maximum)
        {
            if (!node.IsSequence() || node.size() > maximum)
                throw std::runtime_error("Invalid Material source sequence");
            return node.size();
        }
        std::vector<ShaderUniformPathElement> Path(const YAML::Node& node)
        {
            if (!Sequence(node, 17))
                throw std::runtime_error("Empty Material uniform path");
            std::vector<ShaderUniformPathElement> result;
            for (const auto& element : node)
            {
                if (Sequence(element, 2) != 2)
                    throw std::runtime_error("Uniform path requires member/array-element pairs");
                result.push_back({element[0].as<uint32_t>(), element[1].as<uint32_t>()});
            }
            return result;
        }
        template<class T>
        std::vector<T> Scalars(const YAML::Node& node)
        {
            if (!Sequence(node, 16))
                throw std::runtime_error("Empty Material constant");
            std::vector<T> result;
            for (const auto& scalar : node)
                result.push_back(scalar.as<T>());
            return result;
        }
    } // namespace
    std::variant<MaterialData, AssetError> ParseMaterialSource(const std::byte* bytes, size_t size)
    {
        if (!bytes || !size || size > 1024 * 1024)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Material source size"};
        try
        {
            std::string text(reinterpret_cast<const char*>(bytes), size);
            if (text.find('\0') != std::string::npos)
                throw std::runtime_error("NUL in Material source");
            auto documents = YAML::LoadAll(text);
            if (documents.size() != 1)
                throw std::runtime_error("Material source requires one document");
            const auto root = documents.front();
            Fields(root,
                   {"magic",
                    "version",
                    "name",
                    "base_color",
                    "metallic",
                    "roughness",
                    "shader",
                    "textures",
                    "uniforms",
                    "samplers"});
            if (root["magic"].as<std::string>() != "KansMaterial")
                throw std::runtime_error("Invalid Material source magic");
            if (root["version"].as<uint32_t>() != 1)
                return AssetError {AssetErrorCode::UnsupportedVersion, "Unsupported Material source version"};
            MaterialData data;
            data.Name = root["name"].as<std::string>();
            if (Sequence(root["base_color"], 4) != 4)
                throw std::runtime_error("Material color requires four floats");
            for (size_t i = 0; i < 4; ++i)
                data.BaseColor[i] = root["base_color"][i].as<float>();
            data.Metallic  = root["metallic"].as<float>();
            data.Roughness = root["roughness"].as<float>();
            data.Shader    = AssetID::FromString(root["shader"].as<std::string>());
            Sequence(root["textures"], 4096);
            for (const auto& texture : root["textures"])
            {
                Fields(texture, {"semantic", "uv", "asset"});
                data.Textures.push_back({texture["semantic"].as<uint32_t>(),
                                         texture["uv"].as<uint32_t>(),
                                         AssetID::FromString(texture["asset"].as<std::string>())});
            }
            data.Bindings      = MaterialBindingRecipe {};
            size_t descriptors = Sequence(root["uniforms"], 4096), writes = 0;
            Sequence(root["samplers"], 4096 - descriptors);
            for (const auto& input : root["uniforms"])
            {
                Fields(input, {"set", "binding", "element", "parameters", "values"});
                MaterialUniformAssignment uniform;
                uniform.Set     = input["set"].as<uint32_t>();
                uniform.Binding = input["binding"].as<uint32_t>();
                uniform.Element = input["element"].as<uint32_t>();
                writes += Sequence(input["parameters"], 4096 - writes);
                writes += Sequence(input["values"], 4096 - writes);
                for (const auto& parameter : input["parameters"])
                {
                    Fields(parameter, {"parameter", "path"});
                    const auto        name = parameter["parameter"].as<std::string>();
                    MaterialParameter kind;
                    if (name == "base_color")
                        kind = MaterialParameter::BaseColor;
                    else if (name == "metallic")
                        kind = MaterialParameter::Metallic;
                    else if (name == "roughness")
                        kind = MaterialParameter::Roughness;
                    else
                        throw std::runtime_error("Unknown Material parameter");
                    uniform.Parameters.push_back({kind, Path(parameter["path"])});
                }
                for (const auto& value : input["values"])
                {
                    Fields(value, {"type", "path", "values"});
                    ShaderUniformWrite write;
                    write.Path      = Path(value["path"]);
                    const auto type = value["type"].as<std::string>();
                    if (type == "float")
                        write.Values = Scalars<float>(value["values"]);
                    else if (type == "int")
                        write.Values = Scalars<int32_t>(value["values"]);
                    else if (type == "uint")
                        write.Values = Scalars<uint32_t>(value["values"]);
                    else
                        throw std::runtime_error("Unknown Material constant type");
                    uniform.Values.push_back(std::move(write));
                }
                data.Bindings->Uniforms.push_back(std::move(uniform));
            }
            for (const auto& sampler : root["samplers"])
            {
                Fields(sampler, {"set", "binding", "element", "texture"});
                data.Bindings->Textures.push_back({sampler["set"].as<uint32_t>(),
                                                   sampler["binding"].as<uint32_t>(),
                                                   sampler["element"].as<uint32_t>(),
                                                   sampler["texture"].as<uint32_t>()});
            }
            // Reuse Product's structural/finite/reference validation before accepting author input.
            auto valid = EncodeMaterialProduct(data);
            if (auto error = std::get_if<AssetError>(&valid))
                return *error;
            return data;
        }
        catch (const std::exception&)
        {
            return AssetError {AssetErrorCode::InvalidData, "Malformed Material source"};
        }
    }

    namespace
    {
        std::variant<MaterialData, AssetError> ReadMaterialSource(const std::filesystem::path& file)
        {
            auto bytes = ReadProductFile(file, 1024 * 1024);
            if (auto* failure = std::get_if<AssetError>(&bytes))
                return std::move(*failure);
            const auto& input = std::get<std::vector<std::byte>>(bytes);
            return ParseMaterialSource(input.data(), input.size());
        }
    } // namespace

    std::variant<ImportInputs, AssetError> MaterialImporter::DiscoverInputs(const ImportContext& context) const
    {
        auto parsed = ReadMaterialSource(context.SourceFile);
        if (auto* failure = std::get_if<AssetError>(&parsed))
            return std::move(*failure);
        const auto&  data = std::get<MaterialData>(parsed);
        ImportInputs inputs;
        inputs.Products.push_back(data.Shader);
        for (const auto& texture : data.Textures)
            if (std::find(inputs.Products.begin(), inputs.Products.end(), texture.Texture) == inputs.Products.end())
                inputs.Products.push_back(texture.Texture);
        for (AssetID id : inputs.Products)
            if (id.GetSourceID() == context.SourceID)
                return AssetError {AssetErrorCode::InvalidData, "Material references its own source"};
        return inputs;
    }

    std::variant<ImportResult, AssetError> MaterialImporter::Import(const ImportContext& context) const
    {
        if (!context.SourceID.IsValid() || !context.SourceFile.is_absolute() ||
            !context.StagingDirectory.is_absolute() || !context.Settings.empty() || !context.SourceInputs.empty())
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid Material import context"};
        try
        {
            std::error_code error;
            auto            status = std::filesystem::symlink_status(context.StagingDirectory, error);
            if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status) ||
                !std::filesystem::is_empty(context.StagingDirectory, error) || error)
                return AssetError {AssetErrorCode::InvalidArgument, "Material staging must be empty and exclusive"};
            auto parsed = ReadMaterialSource(context.SourceFile);
            if (auto failure = std::get_if<AssetError>(&parsed))
                return *failure;
            const auto& data = std::get<MaterialData>(parsed);
            // Referenced Products must have been built and supplied, with the expected types.
            const auto decoded = [&](AssetID id) -> const std::vector<std::byte>* {
                const auto found = context.ProductBytes.find(id);
                return found == context.ProductBytes.end() ? nullptr : &found->second;
            };
            const auto* shaderBytes = decoded(data.Shader);
            if (!shaderBytes)
                return AssetError {AssetErrorCode::NotFound, "Material Shader Product is not available"};
            auto shader = DecodeShaderProduct(shaderBytes->data(), shaderBytes->size());
            if (auto failure = std::get_if<AssetError>(&shader))
                return AssetError {AssetErrorCode::InvalidData, "Material Shader is not a Shader Product"};
            auto packed = PackMaterialUniforms(data, std::get<ShaderData>(shader).Code, *data.Bindings);
            if (auto failure = std::get_if<AssetError>(&packed))
                return *failure;
            std::set<AssetID> sampled;
            for (const auto& assignment : data.Bindings->Textures)
                sampled.insert(data.Textures[assignment.TextureIndex].Texture);
            std::vector<AssetID> dependencies {data.Shader};
            for (const auto& binding : data.Textures)
            {
                if (std::find(dependencies.begin(), dependencies.end(), binding.Texture) != dependencies.end())
                    continue;
                const auto* textureBytes = decoded(binding.Texture);
                if (!textureBytes)
                    return AssetError {AssetErrorCode::NotFound, "Material Texture Product is not available"};
                auto texture = DecodeTextureProduct(textureBytes->data(), textureBytes->size());
                if (std::holds_alternative<AssetError>(texture))
                    return AssetError {AssetErrorCode::InvalidData, "Material Texture is not a Texture Product"};
                if (sampled.count(binding.Texture) && std::get<TextureData>(texture).Kind != TextureKind::Texture2D)
                    return AssetError {AssetErrorCode::NotImplemented, "Material sampler requires a 2D Texture"};
                dependencies.push_back(binding.Texture);
            }
            auto ids = AllocateLocalIDs(context.PreviousLocalIDs, {"material:root"});
            if (auto failure = std::get_if<AssetError>(&ids))
                return *failure;
            ImportResult result;
            result.LocalIDs  = std::get<LocalIDMap>(std::move(ids));
            const auto local = result.LocalIDs.at("material:root");
            result.Products.push_back(
                {local, AssetType::Material, std::to_string(local) + ".kmat", std::move(dependencies)});
            auto encoded = EncodeMaterialProduct(data);
            if (auto failure = std::get_if<AssetError>(&encoded))
                return *failure;
            auto written = WriteProductFile(context.StagingDirectory / result.Products[0].StagedFile,
                                            std::get<std::vector<std::byte>>(encoded));
            if (auto failure = std::get_if<AssetError>(&written))
                return *failure;
            return result;
        }
        catch (const std::exception&)
        {
            return AssetError {AssetErrorCode::InvalidData, "Cannot import Material source"};
        }
    }
} // namespace Kans
