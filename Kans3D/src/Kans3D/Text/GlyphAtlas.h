#pragma once
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include "FontAsset.h"

namespace Kans
{
    struct GlyphRect
    {
        uint32_t X = 0, Y = 0, Width = 0, Height = 0;
    };
    struct AtlasGlyph
    {
        uint32_t   Glyph = 0, Page = 0;
        GlyphRect  Rect;
        FontBounds Plane;
        float      Advance  = 0;
        uint64_t   LastUsed = 0;
    };
    struct GlyphAtlasPage
    {
        std::vector<uint8_t>   Pixels;
        std::vector<GlyphRect> Dirty;
    };
    // CPU atlas mutations and layout requests run on the font-system owner thread.
    // BeginFrame must precede all layouts/draws; cached layouts must Touch their glyphs each frame.
    class GlyphAtlas
    {
    public:
        static constexpr uint32_t PageSize = 1024;
        explicit GlyphAtlas(const FontAsset&, GlyphAtlasOptions = {});
        void                                            BeginFrame(uint64_t frame);
        void                                            Request(uint32_t glyph);
        void                                            Touch(uint32_t glyph);
        const AtlasGlyph*                               Find(uint32_t glyph) const;
        size_t                                          Rasterize(size_t glyphBudget = 4);
        bool                                            EnsureGlyphs(const std::vector<uint32_t>&);
        size_t                                          PendingCount() const { return m_Queue.size(); }
        uint64_t                                        Generation() const { return m_Generation; }
        const std::vector<GlyphAtlasPage>&              Pages() const { return m_Pages; }
        const std::unordered_map<uint32_t, AtlasGlyph>& Entries() const { return m_Entries; }
        void                                            ClearDirty(uint32_t page) { m_Pages.at(page).Dirty.clear(); }

    private:
        bool                        Insert(const GlyphBitmap&);
        bool                        Allocate(uint32_t w, uint32_t h, uint32_t& page, GlyphRect&);
        const FontAsset&            m_Font;
        GlyphAtlasOptions           m_Options;
        uint64_t                    m_Frame = 0, m_Generation = 1;
        bool                        m_LoggedFull = false;
        std::vector<GlyphAtlasPage> m_Pages;
        struct FreeRect
        {
            uint32_t  Page;
            GlyphRect Rect;
        };
        std::vector<FreeRect>                    m_Free;
        std::unordered_map<uint32_t, AtlasGlyph> m_Entries;
        std::deque<uint32_t>                     m_Queue;
        std::unordered_set<uint32_t>             m_Pending;
    };
} // namespace Kans
