#pragma once
#include <variant>
#include <vector>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetError.h"

namespace Kans
{
    enum class TextureKind : uint16_t
    {
        Texture2D = 0,
        Array2D   = 1,
        Cube      = 2,
        CubeArray = 3,
        Volume3D  = 4
    };
    enum class TexturePixelFormat : uint16_t
    {
        R8          = 0,
        RG8         = 1,
        RGBA8       = 2,
        RGBA32Float = 3
    };
    enum class TextureColorSpace : uint16_t
    {
        Linear = 0,
        SRGB   = 1
    };
    enum class TextureUsage : uint16_t
    {
        Data   = 0,
        Color  = 1,
        Normal = 2
    };
    enum class TextureAlphaMode : uint16_t
    {
        Opaque        = 0,
        Straight      = 1,
        Premultiplied = 2
    };
    enum class TextureOrigin : uint16_t
    {
        TopLeft    = 0,
        BottomLeft = 1
    };

    struct TextureMipData
    {
        // Tightly packed layers, then depth slices, rows, pixels, components.
        // RGBA32Float components are canonical little-endian IEEE binary32 bytes.
        std::vector<std::byte> Pixels;
    };

    struct TextureData
    {
        TextureKind        Kind       = TextureKind::Texture2D;
        TexturePixelFormat Format     = TexturePixelFormat::RGBA8;
        TextureColorSpace  ColorSpace = TextureColorSpace::Linear;
        TextureUsage       Usage      = TextureUsage::Color;
        TextureAlphaMode   AlphaMode  = TextureAlphaMode::Straight;
        TextureOrigin      Origin     = TextureOrigin::TopLeft;
        uint32_t           Width = 0, Height = 0, Depth = 1, Layers = 1;
        // Contiguous levels starting at base. Layers remain constant; volume
        // depth shrinks with width/height. Cube face order: +X,-X,+Y,-Y,+Z,-Z.
        std::vector<TextureMipData> Mips;
    };

    [[nodiscard]] std::variant<uint64_t, AssetError> TextureMipByteSize(const TextureData& texture, uint32_t level);
    [[nodiscard]] std::variant<std::monostate, AssetError> ValidateTextureData(const TextureData& texture);
    // Regenerates the full chain from base data. Cube seam-aware filtering is a
    // separate pending operation; this function rejects Cube/CubeArray input.
    [[nodiscard]] std::variant<TextureData, AssetError> GenerateTextureMips(const TextureData& texture);

    class TextureAsset final : public Asset
    {
    public:
        explicit TextureAsset(TextureData data) : m_Data(std::move(data)) {}
        static AssetType   GetStaticType() { return AssetType::Texture; }
        AssetType          GetAssetType() const override { return GetStaticType(); }
        const TextureData& GetData() const { return m_Data; }

    private:
        TextureData m_Data;
    };
} // namespace Kans
