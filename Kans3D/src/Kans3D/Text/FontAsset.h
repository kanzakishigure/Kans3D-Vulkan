#pragma once
#include <hb.h>
#include <variant>
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Asset/AssetError.h"

namespace Kans
{
    class GlyphAtlas;
    struct GlyphAtlasOptions
    {
        uint32_t MaxPages    = 8;
        uint64_t EvictionAge = 120;
    };
    struct FontSettings
    {
        uint32_t             Face = 0, GlyphSize = 48, PixelRange = 6;
        std::string          Preload = "ascii", Extra;
        std::vector<AssetID> Fallbacks;
    };
    struct FontMetrics
    {
        uint32_t UnitsPerEm = 0;
        float    Ascender = 0, Descender = 0, LineGap = 0, UnderlinePosition = 0, UnderlineThickness = 0;
    };
    // Bounds are left, bottom, right, top in em units; bitmap rows run bottom to top.
    struct FontBounds
    {
        float Left = 0, Bottom = 0, Right = 0, Top = 0;
    };
    struct GlyphBitmap
    {
        uint32_t             Glyph = 0, Width = 0, Height = 0;
        FontBounds           Plane;
        float                Advance = 0;
        std::vector<uint8_t> Pixels; // RGBA8 MTSDF, alpha is true signed distance.
    };
    struct FontData
    {
        std::shared_ptr<const std::vector<std::byte>> Bytes;
        FontSettings                                  Settings;
        FontMetrics                                   Metrics;
        std::vector<GlyphBitmap>                      Preloaded;
    };
    class FontAsset final : public Asset
    {
    public:
        explicit FontAsset(FontData data, GlyphAtlasOptions options = {});
        ~FontAsset() override;
        FontAsset(const FontAsset&)                  = delete;
        FontAsset&       operator=(const FontAsset&) = delete;
        static AssetType GetStaticType() { return AssetType::Font; }
        AssetType        GetAssetType() const override { return GetStaticType(); }
        const FontData&  GetData() const { return m_Data; }
        hb_font_t*       GetFont() const { return m_Font; }
        hb_face_t*       GetFace() const { return m_Face; }
        uint32_t         GlyphIndex(uint32_t codepoint) const;
        GlyphAtlas&      Atlas() const;

    private:
        FontData                    m_Data;
        hb_face_t*                  m_Face = nullptr;
        hb_font_t*                  m_Font = nullptr;
        std::unique_ptr<GlyphAtlas> m_Atlas;
    };
    std::variant<FontData, AssetError> ReadFontData(std::shared_ptr<const std::vector<std::byte>>, FontSettings = {});
    std::variant<GlyphBitmap, AssetError> GenerateGlyph(const FontAsset&, uint32_t glyph);
    std::string                           FontName(hb_face_t*, unsigned nameID);
    // Strict UTF-8 decoding; invalid sequences become U+FFFD without losing source byte offsets.
    struct TextCodepoint
    {
        uint32_t Value, Byte, End;
    };
    std::vector<TextCodepoint> DecodeText(const std::string&);
} // namespace Kans
