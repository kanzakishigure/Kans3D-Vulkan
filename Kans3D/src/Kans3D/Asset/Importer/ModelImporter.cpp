#include "kspch.h"
#include "ModelImporter.h"
#include "Kans3D/Asset/Product/MeshProduct.h"
#include "Kans3D/Asset/Product/ModelProduct.h"
#include "Kans3D/Asset/Runtime/ModelData.h"
#include "Kans3D/Asset/Runtime/StandardShader.h"
#include "ImportedModelData.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <set>

namespace Kans
{
    namespace
    {
        std::string ObjectKey(const char* type, const std::string& name)
        {
            // Length prefix prevents delimiter characters in names from aliasing.
            return std::string(type) + ":" + std::to_string(name.size()) + ":" + name;
        }

        struct StagedProduct
        {
            ProductDescriptor      Descriptor;
            std::vector<std::byte> Bytes;
        };

        struct StagingCleanup
        {
            const std::vector<std::filesystem::path>& Paths;
            size_t                                    Attempted = 0;
            bool                                      Complete  = false;
            ~StagingCleanup()
            {
                if (Complete)
                    return;
                for (size_t i = 0; i < Attempted; ++i)
                {
                    std::error_code ignored;
                    std::filesystem::remove(Paths[i], ignored);
                }
            }
        };

        // Makes repeated keys unique without using array position where possible:
        // a repeated key first gets its discriminator (for meshes, the referencing
        // node path), then an occurrence number if it is still not unique.
        void MakeUnique(std::vector<std::string>& keys, const std::vector<std::string>& discriminators)
        {
            std::map<std::string, size_t> counts;
            for (const auto& key : keys)
                ++counts[key];
            for (size_t i = 0; i < keys.size(); ++i)
                if (counts[keys[i]] > 1 && !discriminators[i].empty())
                    keys[i] += ":" + std::to_string(discriminators[i].size()) + ":" + discriminators[i];
            std::map<std::string, size_t> seen;
            for (auto& key : keys)
                if (const size_t occurrence = seen[key]++; occurrence > 0)
                    key += "#" + std::to_string(occurrence);
        }

        void AddUnique(std::vector<AssetID>& values, AssetID id)
        {
            if (std::find(values.begin(), values.end(), id) == values.end())
                values.push_back(id);
        }
    } // namespace

    namespace
    {
        // Settings: "scale" (positive number, default 1) multiplies the model size.
        struct ModelSettings
        {
            float                          Scale = 1;
            std::map<std::string, AssetID> Materials;
        };

        std::variant<ModelSettings, AssetError> ParseModelSettings(const std::map<std::string, std::string>& settings)
        {
            ModelSettings parsed;
            for (const auto& [key, value] : settings)
            {
                if (key.compare(0, 9, "material.") == 0 && key.compare(9, 9, "material:") == 0)
                {
                    const auto id = AssetID::FromString(value);
                    if (!id.IsValid() || id.ToString() != value)
                        return AssetError {AssetErrorCode::InvalidArgument, "Invalid material remap AssetID: " + value};
                    parsed.Materials.emplace(key.substr(9), id);
                    continue;
                }
                if (key != "scale")
                    return AssetError {AssetErrorCode::InvalidArgument, "Unknown model import setting: " + key};
                char* end = nullptr;
                parsed.Scale = std::strtof(value.c_str(), &end);
                if (value.empty() || *end || !std::isfinite(parsed.Scale) || parsed.Scale <= 0.0f)
                    return AssetError {AssetErrorCode::InvalidArgument, "Model scale must be a positive number"};
            }
            return parsed;
        }
    } // namespace

    std::variant<ImportInputs, AssetError> ModelImporter::DiscoverInputs(const ImportContext& context) const
    {
        if (!context.SourceFile.is_absolute())
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid model import context"};
        auto settings = ParseModelSettings(context.Settings);
        if (const auto* error = std::get_if<AssetError>(&settings))
            return *error;
        auto parsed = ParseModelCpu(context.SourceFile);
        if (const auto* error = std::get_if<AssetError>(&parsed))
            return *error;
        const auto&  model = std::get<ImportedModelData>(parsed);
        ImportInputs inputs;
        inputs.Warnings = model.Warnings;
        std::vector<std::string> slots;
        for (const auto& material : model.Materials)
            slots.push_back(ObjectKey("material", material.Name));
        MakeUnique(slots, std::vector<std::string>(slots.size()));
        for (const auto& [slot, id] : std::get<ModelSettings>(settings).Materials)
        {
            if (std::find(slots.begin(), slots.end(), slot) == slots.end())
                inputs.Warnings.push_back("Unknown material remap slot: " + slot);
            else
                inputs.OptionalProducts.emplace(id, AssetType::Material);
        }
        std::set<std::filesystem::path> conflicted; // referenced with differing settings
        for (const auto& file : model.SourceFiles)
            if (file != context.SourceFile.lexically_normal())
                inputs.SourceFiles.push_back(file);
        for (const auto& material : model.Materials)
            for (const auto& texture : material.Textures)
            {
                if (!texture.Source.empty() && texture.Source[0] == '*')
                    continue; // Embedded texture; reported by the parser.
                // A missing texture leaves the material slot empty instead of failing the model.
                auto resolved = ResolveModelTexture(context.SourceFile, texture.Source);
                if (const auto* error = std::get_if<AssetError>(&resolved))
                {
                    inputs.Warnings.push_back(error->Message);
                    continue;
                }
                auto path = std::get<std::filesystem::path>(std::move(resolved));
                // Suggested import settings: non-color textures must not be decoded as
                // sRGB, and an opaque material's color texture uses its alpha as data,
                // not coverage. A texture referenced with different needs gets none.
                using S                                     = MaterialTextureSemantic;
                const auto                         semantic = static_cast<S>(texture.Semantic);
                std::map<std::string, std::string> hint;
                if (semantic == S::Normal || semantic == S::NormalCamera || semantic == S::Height)
                    hint = {{"usage", "normal"}};
                else if (semantic == S::MetallicRoughness || semantic == S::Metalness ||
                         semantic == S::DiffuseRoughness || semantic == S::AmbientOcclusion || semantic == S::Unknown ||
                         semantic == S::Lightmap)
                    hint = {{"usage", "data"}};
                else if ((semantic == S::BaseColor || semantic == S::Diffuse) && material.Opaque)
                    hint = {{"alpha_mode", "opaque"}};
                const bool first = std::find(inputs.ProductSources.begin(), inputs.ProductSources.end(), path) ==
                                   inputs.ProductSources.end();
                if (first)
                {
                    if (!hint.empty())
                        inputs.ProductSourceSettings[path] = hint;
                    else
                        conflicted.insert(path);
                }
                else if (!conflicted.count(path) && (hint.empty() || inputs.ProductSourceSettings[path] != hint))
                {
                    inputs.ProductSourceSettings.erase(path);
                    conflicted.insert(path);
                }
                if (first)
                {
                    inputs.OptionalProductSources.insert(path);
                    inputs.ProductSources.push_back(std::move(path));
                }
            }
        return inputs;
    }

    std::variant<ImportResult, AssetError> ModelImporter::Import(const ImportContext& context) const
    {
        if (!context.SourceID.IsValid() || !context.SourceFile.is_absolute() || !context.StagingDirectory.is_absolute())
            return AssetError {AssetErrorCode::InvalidArgument, "Invalid model import context"};
        auto scale = ParseModelSettings(context.Settings);
        if (const auto* error = std::get_if<AssetError>(&scale))
            return *error;
        std::vector<std::filesystem::path> allowedFiles {context.SourceFile};
        for (const auto& input : context.SourceInputs)
            allowedFiles.push_back(input.first);
        auto parsed = ParseModelCpu(context.SourceFile, allowedFiles);
        if (const auto* error = std::get_if<AssetError>(&parsed))
            return *error;
        auto& source = std::get<ImportedModelData>(parsed);
        if (std::get<ModelSettings>(scale).Scale != 1.0f)
            ScaleModel(source, std::get<ModelSettings>(scale).Scale);
        std::vector<std::string> materialKeys;
        std::vector<std::string> meshKeys;
        for (const auto& material : source.Materials)
            materialKeys.push_back(ObjectKey("material", material.Name));
        MakeUnique(materialKeys, std::vector<std::string>(materialKeys.size()));

        // Hierarchy path of the first node referencing each mesh.
        std::vector<std::string> nodePaths(source.Nodes.size()), meshPaths(source.Meshes.size());
        for (size_t i = 0; i < source.Nodes.size(); ++i)
        {
            const auto& node = source.Nodes[i];
            nodePaths[i] = (node.Parent >= 0 && static_cast<size_t>(node.Parent) < i ? nodePaths[node.Parent] : "") +
                           "/" + node.Name;
            for (uint32_t mesh : node.Meshes)
                if (mesh < meshPaths.size() && meshPaths[mesh].empty())
                    meshPaths[mesh] = nodePaths[i];
        }
        for (const auto& mesh : source.Meshes)
        {
            // Assimp can split one named source object by material. Preserve that
            // distinction without using a traversal index as persistent identity.
            meshKeys.push_back(ObjectKey("mesh", mesh.Name) + ":" + materialKeys.at(mesh.MaterialIndex));
        }
        MakeUnique(meshKeys, meshPaths);
        std::vector<std::string> keys {"model:root"};
        keys.insert(keys.end(), materialKeys.begin(), materialKeys.end());
        keys.insert(keys.end(), meshKeys.begin(), meshKeys.end());
        auto allocated = AllocateLocalIDs(context.PreviousLocalIDs, keys);
        if (const auto* error = std::get_if<AssetError>(&allocated))
            return *error;
        ImportResult result;
        result.LocalIDs = std::get<LocalIDMap>(std::move(allocated));
        std::vector<StagedProduct> staged;
        const auto idFor = [&](const std::string& key) { return AssetID(context.SourceID, result.LocalIDs.at(key)); };

        for (size_t i = 0; i < source.Materials.size(); ++i)
        {
            const auto&       input = source.Materials[i];
            MaterialData      data {input.Name, input.BaseColor, input.Metallic, input.Roughness, {}};
            data.Render = input.Render;
            ProductDescriptor descriptor {result.LocalIDs.at(materialKeys[i]), AssetType::Material, {}, {}};
            descriptor.StagedFile = std::to_string(descriptor.LocalID) + ".kmaterial";
            for (const auto& texture : input.Textures)
            {
                auto resolved = ResolveModelTexture(context.SourceFile, texture.Source);
                if (std::holds_alternative<AssetError>(resolved))
                    continue; // Reported as a warning by DiscoverInputs.
                const auto& path  = std::get<std::filesystem::path>(resolved);
                const auto  found = context.ProductsBySource.find(path);
                if (found == context.ProductsBySource.end())
                    return AssetError {AssetErrorCode::NotFound,
                                       "Unresolved model texture Product: " + path.u8string()};
                if (!found->second.IsValid())
                    continue; // Optional texture build failure; the coordinator warned.
                data.Textures.push_back({texture.Semantic, texture.UVChannel, found->second});
                AddUnique(descriptor.Dependencies, found->second);
            }
            auto standard = ResolveStandardMaterial(data, input.EmissiveColor);
            if (auto* error = std::get_if<AssetError>(&standard))
                return *error;
            data         = std::get<MaterialData>(std::move(standard));
            auto encoded = EncodeMaterialProduct(data);
            if (const auto* error = std::get_if<AssetError>(&encoded))
                return *error;
            staged.push_back({std::move(descriptor), std::get<std::vector<std::byte>>(std::move(encoded))});
        }

        std::set<std::string> warnedRemaps;
        for (size_t i = 0; i < source.Meshes.size(); ++i)
        {
            const auto& input    = source.Meshes[i];
            const auto  material = idFor(materialKeys.at(input.MaterialIndex));
            AssetID     bound    = material;
            const auto  remap    = std::get<ModelSettings>(scale).Materials.find(materialKeys.at(input.MaterialIndex));
            if (remap != std::get<ModelSettings>(scale).Materials.end())
            {
                const auto bytes = context.ProductBytes.find(remap->second);
                if (bytes != context.ProductBytes.end())
                {
                    if (std::holds_alternative<MaterialData>(
                            DecodeMaterialProduct(bytes->second.data(), bytes->second.size())))
                        bound = remap->second;
                    else if (warnedRemaps.insert(remap->first).second)
                        result.Warnings.push_back("Material remap for slot " + remap->first + " (" +
                                                  remap->second.ToString() +
                                                  ") is not a valid Material; using embedded material");
                }
            }
            MeshData    data {input.Vertices, input.Indices, bound, material};
            auto        encoded = EncodeMeshProduct(data);
            if (const auto* error = std::get_if<AssetError>(&encoded))
                return *error;
            ProductDescriptor descriptor {result.LocalIDs.at(meshKeys[i]), AssetType::StaticMesh, {}, {bound}};
            AddUnique(descriptor.Dependencies, material);
            descriptor.StagedFile = std::to_string(descriptor.LocalID) + ".kmesh";
            staged.push_back({std::move(descriptor), std::get<std::vector<std::byte>>(std::move(encoded))});
        }

        ModelData         model;
        ProductDescriptor descriptor {result.LocalIDs.at("model:root"), AssetType::Model, {}, {}};
        descriptor.StagedFile = std::to_string(descriptor.LocalID) + ".kmodel";
        for (const auto& input : source.Nodes)
        {
            ModelNodeData node {input.Name, input.Parent, input.LocalTransform, {}};
            for (uint32_t mesh : input.Meshes)
            {
                const auto id = idFor(meshKeys.at(mesh));
                node.Meshes.push_back(id);
                AddUnique(descriptor.Dependencies, id);
            }
            model.Nodes.push_back(std::move(node));
        }
        auto encoded = EncodeModelProduct(model);
        if (const auto* error = std::get_if<AssetError>(&encoded))
            return *error;
        staged.push_back({std::move(descriptor), std::get<std::vector<std::byte>>(std::move(encoded))});

        // All parsing, identity resolution and encoding finish before any writes.
        // The coordinator owns this exclusive directory; never overwrite an
        // existing set or write to the source tree through a staging symlink.
        std::error_code error;
        const auto      status = std::filesystem::symlink_status(context.StagingDirectory, error);
        if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status) ||
            !std::filesystem::is_empty(context.StagingDirectory, error) || error)
            return AssetError {AssetErrorCode::InvalidArgument,
                               "Model staging directory must be an empty exclusive directory"};
        std::vector<std::filesystem::path> paths;
        paths.reserve(staged.size());
        result.Products.reserve(staged.size());
        for (const auto& product : staged)
        {
            paths.push_back(context.StagingDirectory / product.Descriptor.StagedFile);
            result.Products.push_back(product.Descriptor);
        }
        StagingCleanup cleanup {paths};
        for (size_t i = 0; i < staged.size(); ++i)
        {
            const auto& product = staged[i];
            const auto& path    = paths[i];
            ++cleanup.Attempted;
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output)
                return AssetError {AssetErrorCode::IoError, "Cannot open staged model Product: " + path.u8string()};
            output.write(reinterpret_cast<const char*>(product.Bytes.data()),
                         static_cast<std::streamsize>(product.Bytes.size()));
            output.flush();
            output.close();
            if (!output)
                return AssetError {AssetErrorCode::IoError, "Cannot write staged model Product: " + path.u8string()};
        }
        cleanup.Complete = true;
        return result;
    }
} // namespace Kans
