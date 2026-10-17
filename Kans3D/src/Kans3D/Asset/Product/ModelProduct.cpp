#include "kspch.h"
#include "ModelProduct.h"
#include "MaterialBindingRecipe.h"
#include "MaterialRenderStateYaml.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <yaml-cpp/yaml.h>

namespace Kans
{
    namespace
    {
        constexpr size_t kMaximumBytes = 64 * 1024 * 1024;
        constexpr size_t kMaximumItems = 1000000;

        bool ValidName(const std::string& name) { return name.size() <= 4096 && name.find('\0') == std::string::npos; }

        template<size_t N>
        bool Finite(const std::array<float, N>& values)
        {
            return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
        }

        template<size_t N>
        void EmitFloats(YAML::Emitter& out, const std::array<float, N>& values)
        {
            out << YAML::Flow << YAML::BeginSeq;
            for (float value : values)
                out << value;
            out << YAML::EndSeq;
        }

        template<size_t N>
        bool ReadFloats(const YAML::Node& node, std::array<float, N>& values)
        {
            if (!node.IsSequence() || node.size() != N)
                return false;
            for (size_t i = 0; i < N; ++i)
                values[i] = node[i].as<float>();
            return Finite(values);
        }

        std::variant<YAML::Node, AssetError>
        ReadRoot(const std::byte* bytes, size_t size, const char* magic, uint32_t maximumVersion = 1)
        {
            if (!bytes || !size || size > kMaximumBytes)
                return AssetError {AssetErrorCode::InvalidData, "Invalid model/material Product size"};
            auto documents = YAML::LoadAll(std::string(reinterpret_cast<const char*>(bytes), size));
            if (documents.size() != 1)
                return AssetError {AssetErrorCode::InvalidData, "Product must contain exactly one document"};
            auto root = documents.front();
            if (!root.IsMap() || !root["magic"].IsScalar() || root["magic"].as<std::string>() != magic ||
                !root["version"].IsScalar())
                return AssetError {AssetErrorCode::InvalidData, "Invalid model/material Product header"};
            if (!root["version"].as<uint32_t>() || root["version"].as<uint32_t>() > maximumVersion)
                return AssetError {AssetErrorCode::UnsupportedVersion, "Unsupported model/material Product version"};
            return root;
        }

        std::variant<std::vector<std::byte>, AssetError> Finish(const YAML::Emitter& out)
        {
            if (!out.good() || out.size() > kMaximumBytes)
                return AssetError {AssetErrorCode::InvalidData,
                                   "Cannot encode model/material Product within size limit"};
            std::vector<std::byte> bytes(out.size());
            std::memcpy(bytes.data(), out.c_str(), bytes.size());
            return bytes;
        }

        bool ValidMaterial(const MaterialData& data)
        {
            return std::holds_alternative<std::monostate>(ValidateMaterialData(data));
        }

        std::variant<std::string, AssetError> RecipeText(const MaterialBindingRecipe& recipe)
        {
            auto encoded = EncodeMaterialBindingRecipe(recipe);
            if (auto error = std::get_if<AssetError>(&encoded))
                return *error;
            const auto& bytes = std::get<std::vector<std::byte>>(encoded);
            std::string text;
            text.reserve(bytes.size() * 2);
            constexpr char digits[] = "0123456789abcdef";
            for (auto byte : bytes)
            {
                auto value = std::to_integer<unsigned char>(byte);
                text.push_back(digits[value >> 4]);
                text.push_back(digits[value & 15]);
            }
            return text;
        }

        std::variant<MaterialBindingRecipe, AssetError> ReadRecipe(const YAML::Node& node)
        {
            if (!node.IsScalar())
                return AssetError {AssetErrorCode::InvalidData, "Material recipe must be a hex scalar"};
            const auto& text = node.Scalar();
            if (text.size() < 32 || text.size() > 2 * 1024 * 1024 || text.size() % 2)
                return AssetError {AssetErrorCode::InvalidData, "Invalid Material recipe size"};
            const auto nibble = [](char value) -> int {
                if (value >= '0' && value <= '9')
                    return value - '0';
                if (value >= 'a' && value <= 'f')
                    return value - 'a' + 10;
                return -1;
            };
            std::vector<std::byte> bytes;
            bytes.reserve(text.size() / 2);
            for (size_t i = 0; i < text.size(); i += 2)
            {
                auto high = nibble(text[i]), low = nibble(text[i + 1]);
                if (high < 0 || low < 0)
                    return AssetError {AssetErrorCode::InvalidData, "Invalid Material recipe hex"};
                bytes.push_back(std::byte((high << 4) | low));
            }
            return DecodeMaterialBindingRecipe(bytes.data(), bytes.size());
        }

        bool ValidModel(const ModelData& data)
        {
            if (data.Nodes.empty() || data.Nodes.size() > kMaximumItems)
                return false;
            size_t bindings = 0;
            for (size_t i = 0; i < data.Nodes.size(); ++i)
            {
                const auto& node = data.Nodes[i];
                if (!ValidName(node.Name) || !Finite(node.LocalTransform) ||
                    (i == 0 ? node.Parent != -1 : node.Parent < 0 || uint64_t(node.Parent) >= i))
                    return false;
                if (node.Meshes.size() > kMaximumItems - bindings)
                    return false;
                bindings += node.Meshes.size();
                for (const auto& id : node.Meshes)
                    if (!id.IsValid())
                        return false;
            }
            if (data.SectionIdentities.size() > kMaximumItems)
                return false;
            std::set<std::pair<AssetID, uint32_t>> sections;

            for (const auto& identity : data.SectionIdentities)
            {
                if (identity.ObjectKey.empty() || !ValidName(identity.ObjectKey) || !identity.OldMesh.IsValid() ||
                    !identity.Mesh.IsValid() || identity.Section >= kMaximumItems ||
                    !sections.emplace(identity.Mesh, identity.Section).second)
                    return false;
            }
            std::map<AssetID, uint32_t> nextSection;
            for (const auto& identity : data.SectionIdentities)
                if (identity.Section != nextSection[identity.Mesh]++)
                    return false;
            return true;
        }
    } // namespace

    std::variant<std::vector<std::byte>, AssetError> EncodeMaterialProduct(const MaterialData& data)
    {
        if (!ValidMaterial(data))
            return AssetError {AssetErrorCode::InvalidData, "Invalid material Product data"};
        std::string recipe;
        if (data.Bindings)
        {
            auto encoded = RecipeText(*data.Bindings);
            if (auto error = std::get_if<AssetError>(&encoded))
                return *error;
            recipe = std::get<std::string>(std::move(encoded));
        }
        YAML::Emitter out;
        out.SetFloatPrecision(9);
        out << YAML::BeginMap << YAML::Key << "magic" << YAML::Value << "KMAT" << YAML::Key << "version" << YAML::Value
            << 4 << YAML::Key << "name" << YAML::Value << data.Name << YAML::Key << "base_color" << YAML::Value;
        EmitFloats(out, data.BaseColor);
        out << YAML::Key << "metallic" << YAML::Value << data.Metallic << YAML::Key << "roughness" << YAML::Value
            << data.Roughness << YAML::Key << "textures" << YAML::Value << YAML::BeginSeq;
        for (const auto& texture : data.Textures)
            out << YAML::BeginMap << YAML::Key << "semantic" << YAML::Value << texture.Semantic << YAML::Key << "uv"
                << YAML::Value << texture.UVChannel << YAML::Key << "asset" << YAML::Value << texture.Texture.ToString()
                << YAML::EndMap;
        out << YAML::EndSeq;
        if (data.Shader.IsValid())
            out << YAML::Key << "shader" << YAML::Value << data.Shader.ToString();
        if (data.Bindings)
            out << YAML::Key << "bindings" << YAML::Value << YAML::DoubleQuoted << recipe;
        EmitMaterialRenderState(out, data.Render);
        out << YAML::EndMap;
        return Finish(out);
    }

    std::variant<MaterialData, AssetError> DecodeMaterialProduct(const std::byte* bytes, size_t size)
    {
        try
        {
            auto loaded = ReadRoot(bytes, size, "KMAT", 4);
            if (const auto* error = std::get_if<AssetError>(&loaded))
                return *error;
            const auto& root          = std::get<YAML::Node>(loaded);
            const auto  version       = root["version"].as<uint32_t>();
            const bool  shaderVersion = version == 4 ? bool(root["shader"]) : version >= 2;
            const bool  recipeVersion = version == 4 ? bool(root["bindings"]) : version == 3;
            if (root.size() != (version == 4  ? 8 + size_t(shaderVersion) + size_t(recipeVersion) :
                                version == 3  ? 9 :
                                shaderVersion ? 8 :
                                                7) ||
                !root["textures"].IsSequence() || root["textures"].size() > kMaximumItems)
                return AssetError {AssetErrorCode::InvalidData, "Invalid material Product fields"};
            MaterialData data;
            if (version == 4)
            {
                if (!root["render"])
                    return AssetError {AssetErrorCode::InvalidData, "Missing material render state"};
                data.Render = ReadMaterialRenderState(root["render"]);
            }
            if (recipeVersion)
            {
                auto recipe = ReadRecipe(root["bindings"]);
                if (auto error = std::get_if<AssetError>(&recipe))
                    return *error;
                data.Bindings = std::get<MaterialBindingRecipe>(std::move(recipe));
            }
            if (shaderVersion)
            {
                data.Shader = AssetID::FromString(root["shader"].as<std::string>());
                if (!data.Shader.IsValid())
                    return AssetError {AssetErrorCode::InvalidData, "Invalid material Shader reference"};
            }
            data.Name = root["name"].as<std::string>();
            if (!ReadFloats(root["base_color"], data.BaseColor))
                return AssetError {AssetErrorCode::InvalidData, "Invalid material color"};
            data.Metallic  = root["metallic"].as<float>();
            data.Roughness = root["roughness"].as<float>();
            for (const auto& texture : root["textures"])
            {
                if (!texture.IsMap() || texture.size() != 3)
                    return AssetError {AssetErrorCode::InvalidData, "Invalid material texture binding"};
                data.Textures.push_back({texture["semantic"].as<uint32_t>(),
                                         texture["uv"].as<uint32_t>(),
                                         AssetID::FromString(texture["asset"].as<std::string>())});
            }
            if (!ValidMaterial(data))
                return AssetError {AssetErrorCode::InvalidData, "Invalid material Product values"};
            return data;
        }
        catch (const std::exception& error)
        {
            return AssetError {AssetErrorCode::InvalidData,
                               "Cannot decode material Product: " + std::string(error.what())};
        }
    }

    std::variant<std::vector<std::byte>, AssetError> EncodeModelProduct(const ModelData& data)
    {
        if (!ValidModel(data))
            return AssetError {AssetErrorCode::InvalidData, "Invalid model Product data"};
        YAML::Emitter out;
        out.SetFloatPrecision(9);
        out << YAML::BeginMap << YAML::Key << "magic" << YAML::Value << "KMOD" << YAML::Key << "version" << YAML::Value
            << 2 << YAML::Key << "nodes" << YAML::Value << YAML::BeginSeq;
        for (const auto& node : data.Nodes)
        {
            out << YAML::BeginMap << YAML::Key << "name" << YAML::Value << node.Name << YAML::Key << "parent"
                << YAML::Value << node.Parent << YAML::Key << "transform" << YAML::Value;
            EmitFloats(out, node.LocalTransform);
            out << YAML::Key << "meshes" << YAML::Value << YAML::BeginSeq;
            for (const auto& mesh : node.Meshes)
                out << mesh.ToString();
            out << YAML::EndSeq << YAML::EndMap;
        }
        out << YAML::EndSeq << YAML::Key << "section_identities" << YAML::Value << YAML::BeginSeq;
        for (const auto& identity : data.SectionIdentities)
            out << YAML::BeginMap << YAML::Key << "object_key" << YAML::Value << identity.ObjectKey << YAML::Key
                << "old_mesh" << YAML::Value << identity.OldMesh.ToString() << YAML::Key << "mesh" << YAML::Value
                << identity.Mesh.ToString() << YAML::Key << "section" << YAML::Value << identity.Section
                << YAML::EndMap;
        out << YAML::EndSeq << YAML::EndMap;
        return Finish(out);
    }

    std::variant<ModelData, AssetError> DecodeModelProduct(const std::byte* bytes, size_t size)
    {
        try
        {
            auto loaded = ReadRoot(bytes, size, "KMOD", 2);
            if (const auto* error = std::get_if<AssetError>(&loaded))
                return *error;
            const auto& root = std::get<YAML::Node>(loaded);
            if (root["version"].as<uint32_t>() != 2)
                return AssetError {AssetErrorCode::UnsupportedVersion, "Unsupported Model Product version"};
            if (root.size() != 4 || !root["section_identities"].IsSequence() ||
                root["section_identities"].size() > kMaximumItems || !root["nodes"].IsSequence() ||
                root["nodes"].size() > kMaximumItems)
                return AssetError {AssetErrorCode::InvalidData, "Invalid model Product fields"};
            ModelData data;
            size_t    bindings = 0;
            for (const auto& input : root["nodes"])
            {
                if (!input.IsMap() || input.size() != 4 || !input["meshes"].IsSequence() ||
                    input["meshes"].size() > kMaximumItems - bindings)
                    return AssetError {AssetErrorCode::InvalidData, "Invalid model node fields"};
                ModelNodeData node;
                node.Name   = input["name"].as<std::string>();
                node.Parent = input["parent"].as<int64_t>();
                if (!ReadFloats(input["transform"], node.LocalTransform))
                    return AssetError {AssetErrorCode::InvalidData, "Invalid model node transform"};
                for (const auto& mesh : input["meshes"])
                    node.Meshes.push_back(AssetID::FromString(mesh.as<std::string>()));
                bindings += node.Meshes.size();
                data.Nodes.push_back(std::move(node));
            }
            for (const auto& input : root["section_identities"])
            {
                if (!input.IsMap() || input.size() != 4)
                    return AssetError {AssetErrorCode::InvalidData, "Invalid model section identity fields"};
                data.SectionIdentities.push_back({input["object_key"].as<std::string>(),
                                                  AssetID::FromString(input["old_mesh"].as<std::string>()),
                                                  AssetID::FromString(input["mesh"].as<std::string>()),
                                                  input["section"].as<uint32_t>()});
            }
            if (!ValidModel(data))
                return AssetError {AssetErrorCode::InvalidData, "Invalid model node hierarchy or references"};
            return data;
        }
        catch (const YAML::Exception& error)
        {
            return AssetError {AssetErrorCode::InvalidData,
                               "Cannot decode model Product: " + std::string(error.what())};
        }
    }
} // namespace Kans
