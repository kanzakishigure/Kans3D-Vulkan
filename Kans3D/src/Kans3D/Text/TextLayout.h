#pragma once
#include <glm/glm.hpp>
#include "FontSystem.h"
#include "GlyphAtlas.h"
namespace Kans
{
    enum class TextHorizontalAlignment
    {
        Left,
        Center,
        Right
    };
    enum class TextVerticalAlignment
    {
        Top,
        Center,
        Bottom
    };
    enum class TextOverflow
    {
        Overflow,
        Wrap,
        Ellipsis,
        ShrinkToFit
    };
    struct TextLayoutRequest
    {
        std::string             Text;
        Ref<const FontAsset>    Font;
        FontSystem*             Fonts = nullptr; // Null uses FontSystem::Default().
        float                   Size  = 16;      // Em size in pixels or world units; all output uses these units.
        std::string             Language;        // Empty infers kana -> ja, hangul -> ko, Han -> zh-Hant, else en.
        float                   WrapWidth = 0, BoxHeight = 0;
        TextHorizontalAlignment Horizontal  = TextHorizontalAlignment::Left;
        TextVerticalAlignment   Vertical    = TextVerticalAlignment::Top;
        float                   LineSpacing = 1, LetterSpacing = 0; // Multiplier, em units.
        TextOverflow            Overflow    = TextOverflow::Wrap;
        float                   MinimumSize = 4;
    };
    struct TextGlyphQuad
    {
        Ref<const FontAsset> Font; // Keeps atlas and HB objects alive.
        GlyphAtlas*          Atlas           = nullptr;
        uint64_t             AtlasGeneration = 0;
        uint32_t             Glyph = 0, DrawGlyph = 0, Page = 0, Cluster = 0;
        glm::vec4            UV {}, Position {}; // left, top, right, bottom; bitmap V decreases downwards.
        bool Ready = false; // Missing glyphs are requested only; rerun after Rasterize changes generation.
    };
    struct TextLine
    {
        uint32_t StartCluster = 0, EndCluster = 0;
        float    BaselineY = 0, Width = 0, Ascent = 0, Descent = 0;
    };
    struct TextCaret
    {
        uint32_t  Byte = 0, Line = 0;
        glm::vec2 Position {};
    };
    struct TextLayout
    {
        std::vector<TextGlyphQuad> Glyphs;
        std::vector<TextLine>      Lines;
        std::vector<TextCaret>     Carets;
        glm::vec4                  Bounds {};
        float                      SizeUsed = 0;
        glm::vec2                  CaretPosition(uint32_t byteOffset) const;
        uint32_t                   NearestByteOffset(glm::vec2 point) const;
        bool                       IsAtlasCurrent() const;
        void                       TouchGlyphs() const;
    };
    // Left-to-right shaping only. RTL and bidi are intentionally outside UI1.
    // Cluster byte offsets refer to original UTF-8, including fallback/script runs.
    // Layout allocates no glyph bitmaps. Rasterize is called separately once per frame.
    TextLayout Layout(const TextLayoutRequest&);
} // namespace Kans
