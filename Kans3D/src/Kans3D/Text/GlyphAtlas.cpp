#include "kspch.h"
#include "GlyphAtlas.h"
#include <cstring>
#include <limits>
namespace Kans
{
    GlyphAtlas::GlyphAtlas(const FontAsset& font, GlyphAtlasOptions options) : m_Font(font), m_Options(options)
    {
        m_Options.MaxPages = std::max(1u, options.MaxPages);
        for (const auto& b : font.GetData().Preloaded)
            Insert(b);
        if (!Find(0))
            Request(0);
    }
    void GlyphAtlas::BeginFrame(uint64_t frame) { m_Frame = std::max(m_Frame, frame); }
    void GlyphAtlas::Touch(uint32_t g)
    {
        auto i = m_Entries.find(g);
        if (i != m_Entries.end())
            i->second.LastUsed = m_Frame;
    }
    void GlyphAtlas::Request(uint32_t g)
    {
        if (Find(g))
        {
            Touch(g);
            return;
        }
        if (m_Pending.insert(g).second)
            m_Queue.push_back(g);
    }
    const AtlasGlyph* GlyphAtlas::Find(uint32_t g) const
    {
        auto i = m_Entries.find(g);
        return i == m_Entries.end() ? nullptr : &i->second;
    }
    bool GlyphAtlas::Allocate(uint32_t w, uint32_t h, uint32_t& page, GlyphRect& rect)
    {
        for (;;)
        {
            size_t   best = m_Free.size();
            uint64_t area = std::numeric_limits<uint64_t>::max();
            for (size_t i = 0; i < m_Free.size(); ++i)
            {
                const auto& r = m_Free[i].Rect;
                if (r.Width >= w && r.Height >= h && uint64_t(r.Width) * r.Height < area)
                {
                    best = i;
                    area = uint64_t(r.Width) * r.Height;
                }
            }
            if (best != m_Free.size())
            {
                auto f = m_Free[best];
                m_Free.erase(m_Free.begin() + best);
                page = f.Page;
                rect = {f.Rect.X, f.Rect.Y, w, h};
                // Guillotine split: disjoint right strip and upper strip, including glyph padding.
                if (f.Rect.Width > w)
                    m_Free.push_back({page, {f.Rect.X + w, f.Rect.Y, f.Rect.Width - w, h}});
                if (f.Rect.Height > h)
                    m_Free.push_back({page, {f.Rect.X, f.Rect.Y + h, f.Rect.Width, f.Rect.Height - h}});
                return true;
            }
            if (m_Pages.size() < m_Options.MaxPages)
            {
                page = uint32_t(m_Pages.size());
                m_Pages.push_back({std::vector<uint8_t>(size_t(PageSize) * PageSize * 4), {}});
                m_Free.push_back({page, {0, 0, PageSize, PageSize}});
                continue;
            }
            auto victim = m_Entries.end();
            for (auto i = m_Entries.begin(); i != m_Entries.end(); ++i)
                if (i->first != 0 && i->second.LastUsed < m_Frame &&
                    m_Frame - i->second.LastUsed >= m_Options.EvictionAge &&
                    (victim == m_Entries.end() || i->second.LastUsed < victim->second.LastUsed))
                    victim = i;
            if (victim == m_Entries.end())
                return false;
            auto old = victim->second;
            m_Entries.erase(victim);
            ++m_Generation;
            if (old.Rect.Width && old.Rect.Height)
            {
                auto& p = m_Pages[old.Page];
                for (uint32_t y = 0; y < old.Rect.Height; ++y)
                    std::memset(p.Pixels.data() + ((size_t(old.Rect.Y + y) * PageSize + old.Rect.X) * 4),
                                0,
                                old.Rect.Width * 4);
                p.Dirty.push_back(old.Rect);
                m_Free.push_back({old.Page, old.Rect});
                // Merge adjacent holes so larger requests can reuse space freed by several glyphs.
                bool merged = true;
                while (merged)
                {
                    merged = false;
                    for (size_t i = 0; i < m_Free.size() && !merged; ++i)
                        for (size_t j = i + 1; j < m_Free.size(); ++j)
                        {
                            auto& a = m_Free[i];
                            auto  b = m_Free[j];
                            if (a.Page != b.Page)
                                continue;
                            auto& r = a.Rect;
                            auto  s = b.Rect;
                            if (r.Y == s.Y && r.Height == s.Height && (r.X + r.Width == s.X || s.X + s.Width == r.X))
                            {
                                r.X = std::min(r.X, s.X);
                                r.Width += s.Width;
                            }
                            else if (r.X == s.X && r.Width == s.Width &&
                                     (r.Y + r.Height == s.Y || s.Y + s.Height == r.Y))
                            {
                                r.Y = std::min(r.Y, s.Y);
                                r.Height += s.Height;
                            }
                            else
                                continue;
                            m_Free.erase(m_Free.begin() + j);
                            merged = true;
                            break;
                        }
                }
            }
        }
    }
    bool GlyphAtlas::Insert(const GlyphBitmap& b)
    {
        uint32_t  page = 0;
        GlyphRect rect {};
        if (b.Width && b.Height && !Allocate(b.Width, b.Height, page, rect))
            return false;
        ++m_Generation;
        m_Entries[b.Glyph] = {b.Glyph, page, rect, b.Plane, b.Advance, m_Frame};
        if (b.Width && b.Height)
        {
            auto& p = m_Pages[page];
            for (uint32_t y = 0; y < b.Height; ++y)
                std::memcpy(p.Pixels.data() + ((size_t(rect.Y + y) * PageSize + rect.X) * 4),
                            b.Pixels.data() + size_t(y) * b.Width * 4,
                            b.Width * 4);
            p.Dirty.push_back(rect);
        }
        return true;
    }
    size_t GlyphAtlas::Rasterize(size_t budget)
    {
        size_t generated = 0;
        size_t attempts  = std::min(budget, m_Queue.size());
        while (attempts--)
        {
            auto g = m_Queue.front();
            m_Queue.pop_front();
            m_Pending.erase(g);
            if (Find(g))
                continue;
            auto bitmap  = GenerateGlyph(m_Font, g);
            bool success = false;
            if (auto* b = std::get_if<GlyphBitmap>(&bitmap))
            {
                success = Insert(*b);
                // Full pages may become evictable next frame. Keep the request so
                // a cached .notdef layout can recover without another Layout call.
                if (!success && m_Pending.insert(g).second)
                    m_Queue.push_back(g);
            }
            if (success)
                ++generated;
            else if (!m_LoggedFull)
            {
                m_LoggedFull = true;
                if (Log::GetCoreLogger())
                    CORE_ERROR("Font atlas exhausted or glyph invalid; rendering .notdef");
                else
                    std::cerr << "Font atlas exhausted or glyph invalid; rendering .notdef\n";
            }
        }
        return generated;
    }
    bool GlyphAtlas::EnsureGlyphs(const std::vector<uint32_t>& glyphs)
    {
        for (auto g : glyphs)
            Request(g);
        Rasterize(m_Queue.size());
        for (auto g : glyphs)
            if (!Find(g))
                return false;
        return true;
    }
} // namespace Kans
