#include "kspch.h"
#include "TextLayout.h"
#include <cmath>
#include <limits>
namespace Kans
{
    namespace
    {
        bool CJK(uint32_t c)
        {
            return (c >= 0x3400 && c <= 0x9fff) || (c >= 0x20000 && c <= 0x323af) || (c >= 0x3040 && c <= 0x30ff) ||
                   (c >= 0xac00 && c <= 0xd7af) || (c >= 0xff66 && c <= 0xff9d);
        }
        bool Contains(const std::vector<TextCodepoint>& set, uint32_t cp)
        {
            for (auto c : set)
                if (c.Value == cp)
                    return true;
            return false;
        }
        bool Closing(uint32_t c)
        {
            static const auto set = DecodeText(u8"，。、；：？！）」』】〉》〕…ー､｡･｝｣,.;:!?)]}");
            return Contains(set, c);
        }
        bool Opening(uint32_t c)
        {
            static const auto set = DecodeText(u8"（「『【〈《〔｛｢([{");
            return Contains(set, c);
        }
        struct Shaped
        {
            Ref<const FontAsset> Font;
            uint32_t             Glyph, Cluster;
            float                Advance, X, Y;
        };
        struct Cluster
        {
            uint32_t            Byte = 0, End = 0, First = 0, Last = 0;
            float               Advance = 0;
            bool                Newline = false, Space = false;
            std::vector<Shaped> Glyphs;
        };
        std::string Infer(const std::vector<TextCodepoint>& text)
        {
            for (auto c : text)
                if (c.Value >= 0x3040 && c.Value <= 0x30ff)
                    return "ja";
            for (auto c : text)
                if (c.Value >= 0xac00 && c.Value <= 0xd7af)
                    return "ko";
            for (auto c : text)
                if (CJK(c.Value))
                    return "zh-Hant";
            return "en";
        }
        Ref<const FontAsset> Choose(const std::vector<Ref<const FontAsset>>& fonts, uint32_t cp)
        {
            for (auto& f : fonts)
                if (f->GlyphIndex(cp))
                    return f;
            return fonts.empty() ? Ref<const FontAsset> {} : fonts.front();
        }
        std::vector<Cluster> Shape(const std::string&                       text,
                                   const std::vector<TextCodepoint>&        cps,
                                   const std::vector<Ref<const FontAsset>>& fonts,
                                   const std::string&                       language)
        {
            std::vector<Cluster> clusters;
            clusters.reserve(cps.size());
            if (fonts.empty())
                return clusters;
            std::vector<Ref<const FontAsset>> selected;
            std::vector<hb_script_t>          scripts;
            selected.reserve(cps.size());
            scripts.reserve(cps.size());
            auto*       unicode = hb_unicode_funcs_get_default();
            hb_script_t prior   = HB_SCRIPT_LATIN;
            for (auto cp : cps)
            {
                selected.push_back(Choose(fonts, cp.Value));
                auto script = hb_unicode_script(unicode, cp.Value);
                if (script == HB_SCRIPT_COMMON || script == HB_SCRIPT_INHERITED || script == HB_SCRIPT_UNKNOWN)
                    script = prior;
                else
                    prior = script;
                scripts.push_back(script);
            }
            auto* buffer = hb_buffer_create();
            for (size_t begin = 0; begin < cps.size();)
            {
                if (cps[begin].Value == '\n')
                {
                    Cluster c;
                    c.Byte  = cps[begin].Byte;
                    c.End   = cps[begin].End;
                    c.First = c.Last = '\n';
                    c.Newline        = true;
                    clusters.push_back(c);
                    ++begin;
                    continue;
                }
                size_t end = begin + 1;
                while (end < cps.size() && cps[end].Value != '\n' && selected[end] == selected[begin] &&
                       scripts[end] == scripts[begin])
                    ++end;
                hb_buffer_reset(buffer);
                hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
                hb_buffer_set_script(buffer, scripts[begin]);
                hb_buffer_set_language(buffer, hb_language_from_string(language.c_str(), -1));
                hb_buffer_set_cluster_level(buffer, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_GRAPHEMES);
                hb_buffer_set_content_type(buffer, HB_BUFFER_CONTENT_TYPE_UNICODE);
                for (size_t i = begin; i < end; ++i)
                    hb_buffer_add(buffer, cps[i].Value, cps[i].Byte);
                hb_shape(selected[begin]->GetFont(), buffer, nullptr, 0);
                unsigned n        = 0;
                auto*    info     = hb_buffer_get_glyph_infos(buffer, &n);
                auto*    pos      = hb_buffer_get_glyph_positions(buffer, &n);
                float    em       = float(hb_face_get_upem(selected[begin]->GetFace()));
                size_t   cpCursor = begin;
                for (unsigned i = 0; i < n;)
                {
                    unsigned j = i + 1;
                    while (j < n && info[j].cluster == info[i].cluster)
                        ++j;
                    Cluster c;
                    c.Byte = info[i].cluster;
                    c.End  = j < n ? info[j].cluster : cps[end - 1].End;
                    while (cpCursor < end && cps[cpCursor].Byte < c.Byte)
                        ++cpCursor;
                    while (cpCursor < end && cps[cpCursor].Byte < c.End)
                    {
                        if (!c.First)
                            c.First = cps[cpCursor].Value;
                        c.Last = cps[cpCursor].Value;
                        ++cpCursor;
                    }
                    c.Space = c.First == ' ' || c.First == '\t' || c.First == '\r';
                    for (unsigned k = i; k < j; ++k)
                    {
                        c.Glyphs.push_back({selected[begin],
                                            info[k].codepoint,
                                            info[k].cluster,
                                            pos[k].x_advance / em,
                                            pos[k].x_offset / em,
                                            pos[k].y_offset / em});
                        c.Advance += pos[k].x_advance / em;
                    }
                    clusters.push_back(std::move(c));
                    i = j;
                }
                begin = end;
            }
            hb_buffer_destroy(buffer);
            return clusters;
        }
        bool Break(const Cluster& a, const Cluster& b)
        {
            if (Opening(a.Last) || Closing(b.First))
                return false;
            return a.Space || a.Last == '-' || CJK(a.Last) || CJK(b.First);
        }
        struct LineRange
        {
            size_t Begin, End;
            bool   Ellipsis = false;
        };
        std::vector<LineRange> Split(const std::vector<Cluster>& c, float size, const TextLayoutRequest& r)
        {
            std::vector<LineRange> lines;
            size_t                 begin = 0;
            bool                   wrap =
                (r.Overflow == TextOverflow::Wrap || r.Overflow == TextOverflow::ShrinkToFit) && r.WrapWidth > 0;
            while (begin < c.size())
            {
                size_t end = begin, lastBreak = begin;
                float  width = 0;
                for (; end < c.size() && !c[end].Newline; ++end)
                {
                    float next = width + c[end].Advance * size + (end > begin ? r.LetterSpacing * size : 0);
                    if (wrap && end > begin && !c[end].Space && next > r.WrapWidth + 0.0001f)
                    {
                        if (lastBreak > begin)
                            end = lastBreak;
                        // A forced long-word break also obeys kinsoku; backtrack connected punctuation.
                        while (end > begin && end < c.size() && (Closing(c[end].First) || Opening(c[end - 1].Last)))
                            --end;
                        if (end == begin)
                        {
                            end = begin + 1;
                            while (end < c.size() && !c[end].Newline &&
                                   (Closing(c[end].First) || Opening(c[end - 1].Last)))
                                ++end;
                        }
                        break;
                    }
                    width = next;
                    if (end + 1 < c.size() && Break(c[end], c[end + 1]))
                        lastBreak = end + 1;
                }
                auto visible = end;
                while (visible > begin && c[visible - 1].Space)
                    --visible;
                lines.push_back({begin, visible, false});
                if (end < c.size() && c[end].Newline)
                {
                    begin = end + 1;
                    if (begin == c.size())
                        lines.push_back({begin, begin, false});
                }
                else
                {
                    begin = end;
                    while (begin < c.size() && c[begin].Space)
                        ++begin;
                }
            }
            if (lines.empty())
                lines.push_back({0, 0, false});
            return lines;
        }
        float Width(const std::vector<Cluster>& c, size_t begin, size_t end, float size, float spacing)
        {
            float w = 0;
            for (size_t i = begin; i < end; ++i)
                w += (c[i].Advance + spacing) * size;
            if (end > begin)
                w -= spacing * size;
            return w;
        }
        TextLayout Build(const TextLayoutRequest&    r,
                         float                       size,
                         const std::vector<Cluster>& clusters,
                         const std::vector<Cluster>& ellipsis,
                         const Ref<const FontAsset>& defaultFont)
        {
            auto       ranges = Split(clusters, size, r);
            TextLayout out;
            out.SizeUsed = size;
            float y      = 0;
            for (auto range : ranges)
            {
                if (r.Overflow == TextOverflow::Ellipsis && r.WrapWidth > 0 &&
                    Width(clusters, range.Begin, range.End, size, r.LetterSpacing) > r.WrapWidth)
                {
                    range.Ellipsis = true;
                    float ew       = Width(ellipsis, 0, ellipsis.size(), size, r.LetterSpacing);
                    while (range.End > range.Begin && Width(clusters, range.Begin, range.End, size, r.LetterSpacing) +
                                                              ew +
                                                              (range.End > range.Begin ? r.LetterSpacing * size : 0) >
                                                          r.WrapWidth)
                        --range.End;
                }
                TextLine line;
                line.StartCluster =
                    range.Begin < clusters.size() ? clusters[range.Begin].Byte : uint32_t(r.Text.size());
                line.EndCluster = range.End > range.Begin ? clusters[range.End - 1].End : line.StartCluster;
                line.Width      = Width(clusters, range.Begin, range.End, size, r.LetterSpacing);
                if (range.Ellipsis)
                    line.Width += Width(ellipsis, 0, ellipsis.size(), size, r.LetterSpacing) +
                                  (range.End > range.Begin ? r.LetterSpacing * size : 0);
                auto metric = [&](const Ref<const FontAsset>& f) {
                    if (!f)
                        return;
                    const auto& m = f->GetData().Metrics;
                    line.Ascent   = std::max(line.Ascent, m.Ascender * size);
                    line.Descent  = std::max(line.Descent, -m.Descender * size);
                };
                metric(defaultFont);
                float gap = 0;
                for (size_t i = range.Begin; i < range.End; ++i)
                    for (const auto& g : clusters[i].Glyphs)
                    {
                        metric(g.Font);
                        gap = std::max(gap, g.Font->GetData().Metrics.LineGap * size);
                    }
                if (range.Ellipsis)
                    for (const auto& c : ellipsis)
                        for (const auto& g : c.Glyphs)
                            metric(g.Font);
                line.BaselineY = y + line.Ascent;
                float x        = 0;
                if (r.WrapWidth > 0)
                {
                    if (r.Horizontal == TextHorizontalAlignment::Center)
                        x = (r.WrapWidth - line.Width) * .5f;
                    else if (r.Horizontal == TextHorizontalAlignment::Right)
                        x = r.WrapWidth - line.Width;
                }
                auto emit = [&](const Cluster& c, uint32_t source) {
                    out.Carets.push_back({source, uint32_t(out.Lines.size()), {x, line.BaselineY}});
                    for (const auto& g : c.Glyphs)
                    {
                        auto& atlas = g.Font->Atlas();
                        atlas.Request(g.Glyph);
                        auto*    entry = atlas.Find(g.Glyph);
                        uint32_t draw  = g.Glyph;
                        if (!entry)
                        {
                            entry = atlas.Find(0);
                            draw  = 0;
                            atlas.Touch(0);
                        }
                        TextGlyphQuad q;
                        q.Font            = g.Font;
                        q.Atlas           = &atlas;
                        q.Glyph           = g.Glyph;
                        q.DrawGlyph       = draw;
                        q.Cluster         = source;
                        q.AtlasGeneration = atlas.Generation();
                        q.Ready           = draw == g.Glyph && entry != nullptr; // .notdef stands in until rasterized.
                        FontBounds b;
                        if (entry)
                        {
                            b                = entry->Plane;
                            q.Page           = entry->Page;
                            auto        rect = entry->Rect;
                            const float page = float(GlyphAtlas::PageSize);
                            q.UV             = {float(rect.X) / page,
                                                float(rect.Y + rect.Height) / page,
                                                float(rect.X + rect.Width) / page,
                                                float(rect.Y) / page};
                        }
                        else
                        {
                            hb_glyph_extents_t e {};
                            hb_font_get_glyph_extents(g.Font->GetFont(), g.Glyph, &e);
                            float em = float(hb_face_get_upem(g.Font->GetFace()));
                            b        = {e.x_bearing / em,
                                        (e.y_bearing + e.height) / em,
                                        (e.x_bearing + e.width) / em,
                                        e.y_bearing / em};
                        }
                        q.Position = {x + (g.X + b.Left) * size,
                                      line.BaselineY - (g.Y + b.Top) * size,
                                      x + (g.X + b.Right) * size,
                                      line.BaselineY - (g.Y + b.Bottom) * size};
                        out.Glyphs.push_back(std::move(q));
                        x += g.Advance * size;
                    }
                    x += r.LetterSpacing * size;
                };
                for (size_t i = range.Begin; i < range.End; ++i)
                    emit(clusters[i], clusters[i].Byte);
                if (range.Ellipsis)
                    for (const auto& c : ellipsis)
                        emit(c, line.EndCluster);
                if (range.End > range.Begin || range.Ellipsis)
                    x -= r.LetterSpacing * size;
                out.Carets.push_back({range.Ellipsis ? uint32_t(r.Text.size()) : line.EndCluster,
                                      uint32_t(out.Lines.size()),
                                      {x, line.BaselineY}});
                out.Lines.push_back(line);
                out.Bounds.z = std::max(out.Bounds.z, line.Width);
                out.Bounds.w = y + line.Ascent + line.Descent;
                y += (line.Ascent + line.Descent + gap) * r.LineSpacing;
            }
            float shift = 0;
            if (r.BoxHeight > 0)
            {
                if (r.Vertical == TextVerticalAlignment::Center)
                    shift = (r.BoxHeight - out.Bounds.w) * .5f;
                else if (r.Vertical == TextVerticalAlignment::Bottom)
                    shift = r.BoxHeight - out.Bounds.w;
            }
            float minX = std::numeric_limits<float>::max(), maxX = -minX;
            for (auto& line : out.Lines)
            {
                line.BaselineY += shift;
                float left = r.Horizontal == TextHorizontalAlignment::Center && r.WrapWidth > 0 ?
                                 (r.WrapWidth - line.Width) / 2 :
                             r.Horizontal == TextHorizontalAlignment::Right && r.WrapWidth > 0 ?
                                 r.WrapWidth - line.Width :
                                 0;
                minX       = std::min(minX, left);
                maxX       = std::max(maxX, left + line.Width);
            }
            for (auto& q : out.Glyphs)
            {
                q.Position.y += shift;
                q.Position.w += shift;
            }
            for (auto& c : out.Carets)
                c.Position.y += shift;
            out.Bounds = {minX, shift, maxX, out.Bounds.w + shift};
            return out;
        }
    } // namespace
    TextLayout Layout(const TextLayoutRequest& r)
    {
        if (!std::isfinite(r.Size) || r.Size <= 0 || !std::isfinite(r.WrapWidth) || r.WrapWidth < 0 ||
            !std::isfinite(r.BoxHeight) || r.BoxHeight < 0 || !std::isfinite(r.LineSpacing) || r.LineSpacing <= 0 ||
            !std::isfinite(r.LetterSpacing))
            return {};
        auto  cps      = DecodeText(r.Text);
        auto  language = r.Language.empty() ? Infer(cps) : r.Language;
        auto& fonts    = r.Fonts ? *r.Fonts : FontSystem::Default();
        auto  chain    = fonts.Chain(r.Font, language);
        if (chain.empty())
            return {};
        auto        shaped = Shape(r.Text, cps, chain, language);
        std::string ell    = u8"…";
        bool        has    = false;
        for (auto& f : chain)
            if (f->GlyphIndex(0x2026))
            {
                has = true;
                break;
            }
        if (!has)
            ell = "...";
        auto ellipsis = Shape(ell, DecodeText(ell), chain, language);
        auto out      = Build(r, r.Size, shaped, ellipsis, chain.front());
        if (r.Overflow == TextOverflow::ShrinkToFit)
        {
            auto fits = [&](const TextLayout& l) {
                return (r.WrapWidth <= 0 || l.Bounds.z - l.Bounds.x <= r.WrapWidth + 0.00001f) &&
                       (r.BoxHeight <= 0 || l.Bounds.w - l.Bounds.y <= r.BoxHeight + 0.00001f);
            };
            if (!fits(out))
            {
                float low  = std::clamp(std::isfinite(r.MinimumSize) ? r.MinimumSize : 4.f, 0.001f, r.Size),
                      high = r.Size;
                out        = Build(r, low, shaped, ellipsis, chain.front());
                if (fits(out))
                    for (int i = 0; i < 24; ++i)
                    {
                        float mid       = (low + high) * .5f;
                        auto  candidate = Build(r, mid, shaped, ellipsis, chain.front());
                        if (fits(candidate))
                        {
                            low = mid;
                            out = std::move(candidate);
                        }
                        else
                            high = mid;
                    }
            }
        }
        return out;
    }
    glm::vec2 TextLayout::CaretPosition(uint32_t byte) const
    {
        if (Carets.empty())
            return {};
        const auto* best = &Carets.front();
        for (const auto& c : Carets)
            if (c.Byte <= byte && c.Byte >= best->Byte)
                best = &c;
        return best->Position;
    }
    uint32_t TextLayout::NearestByteOffset(glm::vec2 p) const
    {
        if (Carets.empty())
            return 0;
        uint32_t line = 0;
        float    best = std::numeric_limits<float>::max();
        for (uint32_t i = 0; i < Lines.size(); ++i)
        {
            float top = Lines[i].BaselineY - Lines[i].Ascent, bottom = Lines[i].BaselineY + Lines[i].Descent;
            float d = p.y < top ? top - p.y : p.y > bottom ? p.y - bottom : 0;
            if (d < best)
            {
                best = d;
                line = i;
            }
        }
        best          = std::numeric_limits<float>::max();
        uint32_t byte = 0;
        for (const auto& c : Carets)
            if (c.Line == line)
            {
                float d = std::abs(c.Position.x - p.x);
                if (d < best)
                {
                    best = d;
                    byte = c.Byte;
                }
            }
        return byte;
    }
    bool TextLayout::IsAtlasCurrent() const
    {
        for (const auto& g : Glyphs)
            if (g.Atlas->Generation() != g.AtlasGeneration)
                return false;
        return true;
    }
    void TextLayout::TouchGlyphs() const
    {
        for (const auto& g : Glyphs)
        {
            g.Atlas->Touch(g.Glyph);
            g.Atlas->Touch(g.DrawGlyph);
        }
    }
} // namespace Kans
