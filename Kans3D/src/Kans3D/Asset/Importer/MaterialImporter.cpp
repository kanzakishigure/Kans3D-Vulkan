#include "kspch.h"
#include "MaterialImporter.h"
#include <set>
#include "Kans3D/Asset/Product/ContentDigest.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Product/ShaderProduct.h"
#include "Kans3D/Asset/Product/TextureProduct.h"
#include "Kans3D/Asset/Storage/ProductFiles.h"

namespace Kans
{
    namespace
    {
        std::variant<MaterialSource, AssetError> ReadMaterialSource(const std::filesystem::path& file)
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
        const auto&  source = std::get<MaterialSource>(parsed);
        ImportInputs inputs;
        if (source.Shader.IsValid())
            inputs.Products.push_back(source.Shader);
        for (const auto& texture : source.Textures)
            if (std::find(inputs.Products.begin(), inputs.Products.end(), texture.second) == inputs.Products.end())
                inputs.Products.push_back(texture.second);
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
            const auto& source = std::get<MaterialSource>(parsed);
            // Referenced Products must have been built and supplied, with the expected types.
            const auto decoded = [&](AssetID id) -> const std::vector<std::byte>* {
                const auto found = context.ProductBytes.find(id);
                return found == context.ProductBytes.end() ? nullptr : &found->second;
            };
            std::optional<ShaderData> shader;
            if (source.Shader.IsValid())
            {
                const auto* shaderBytes = decoded(source.Shader);
                if (!shaderBytes)
                    return AssetError {AssetErrorCode::NotFound, "Material Shader Product is not available"};
                auto decodedShader = DecodeShaderProduct(shaderBytes->data(), shaderBytes->size());
                if (std::holds_alternative<AssetError>(decodedShader))
                    return AssetError {AssetErrorCode::InvalidData, "Material Shader is not a Shader Product"};
                shader = std::get<ShaderData>(std::move(decodedShader));
            }
            auto resolved = ResolveMaterialSource(source, shader ? &*shader : nullptr);
            if (auto* failure = std::get_if<AssetError>(&resolved))
                return std::move(*failure);
            const auto& data = std::get<MaterialData>(resolved);
            std::set<AssetID> sampled;
            if (data.Bindings)
                for (const auto& assignment : data.Bindings->Textures)
                    sampled.insert(data.Textures[assignment.TextureIndex].Texture);
            else
                for (const auto& binding : data.Textures)
                    sampled.insert(binding.Texture);
            std::vector<AssetID> dependencies;
            if (data.Shader.IsValid())
                dependencies.push_back(data.Shader);
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
