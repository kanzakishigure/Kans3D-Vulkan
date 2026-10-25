#pragma once
#include <cstddef>
#include <variant>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetError.h"

namespace Kans
{
    class AssetLoadContext
    {
    public:
        AssetID   ID;
        AssetType Type = AssetType::None;
        // Borrowed Product bytes; caller owns storage for the duration of Load().
        const std::byte* Data = nullptr;
        size_t           Size = 0;
    };

    template<typename T>
    class Loader;
    class MeshAsset;
    class ModelAsset;
    class MaterialDataAsset;
    class TextureAsset;
    class ShaderDataAsset;
    template<>
    class Loader<ShaderDataAsset>
    {
    public:
        std::variant<Ref<ShaderDataAsset>, AssetError> Load(const AssetLoadContext& context) const;
    };
    template<>
    class Loader<TextureAsset>
    {
    public:
        [[nodiscard]] std::variant<Ref<TextureAsset>, AssetError> Load(const AssetLoadContext& context) const;
    };
    template<>
    class Loader<ModelAsset>
    {
    public:
        [[nodiscard]] std::variant<Ref<ModelAsset>, AssetError> Load(const AssetLoadContext& context) const;
    };
    template<>
    class Loader<MaterialDataAsset>
    {
    public:
        [[nodiscard]] std::variant<Ref<MaterialDataAsset>, AssetError> Load(const AssetLoadContext& context) const;
    };
    template<>
    class Loader<MeshAsset>
    {
    public:
        // CPU-only Product decoding. No source paths, file IO, importer or GPU calls.
        [[nodiscard]] std::variant<Ref<MeshAsset>, AssetError> Load(const AssetLoadContext& context) const;
    };
} // namespace Kans
