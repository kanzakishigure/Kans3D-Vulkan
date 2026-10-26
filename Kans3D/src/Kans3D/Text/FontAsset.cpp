#include "kspch.h"
#include "FontAsset.h"
#include <hb-ot.h>
#include <msdfgen.h>
#include <cmath>
#include "GlyphAtlas.h"

namespace Kans
{
    std::vector<TextCodepoint> DecodeText(const std::string& s)
    {
        std::vector<TextCodepoint> out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size();)
        {
            size_t   start = i;
            auto     c     = uint8_t(s[i++]);
            uint32_t cp    = c;
            int      n     = 0;
            if (c >= 0xc2 && c <= 0xdf)
            {
                cp = c & 31;
                n  = 1;
            }
            else if (c >= 0xe0 && c <= 0xef)
            {
                cp = c & 15;
                n  = 2;
            }
            else if (c >= 0xf0 && c <= 0xf4)
            {
                cp = c & 7;
                n  = 3;
            }
            else if (c >= 128)
                cp = 0xfffd;
            bool valid = i + size_t(n) <= s.size();
            for (int j = 0; valid && j < n; ++j)
            {
                auto b = uint8_t(s[i + j]);
                valid  = (b & 192) == 128;
                cp     = (cp << 6) | (b & 63);
            }
            if (n && (!valid || (n == 1 && cp < 128) || (n == 2 && cp < 2048) || (n == 3 && cp < 65536) ||
                      cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)))
                cp = 0xfffd;
            else
                i += n;
            out.push_back({cp, uint32_t(start), uint32_t(i)});
        }
        return out;
    }
    std::string FontName(hb_face_t* face, unsigned id)
    {
        unsigned len = 0;
        // Prefer English; collection regional names are distinct in the English name table.
        auto     language = hb_language_from_string("en", -1);
        unsigned needed   = hb_ot_name_get_utf8(face, hb_ot_name_id_t(id), language, &len, nullptr);
        if (!needed)
        {
            unsigned count = 0;
            auto*    names = hb_ot_name_list_names(face, &count);
            for (unsigned i = 0; i < count; ++i)
                if (names[i].name_id == id)
                {
                    language = names[i].language;
                    needed   = hb_ot_name_get_utf8(face, hb_ot_name_id_t(id), language, &len, nullptr);
                    break;
                }
        }
        std::string s(needed + 1, '\0');
        len = unsigned(s.size());
        hb_ot_name_get_utf8(face, hb_ot_name_id_t(id), language, &len, s.data());
        s.resize(len);
        return s;
    }
    FontAsset::FontAsset(FontData data, GlyphAtlasOptions options) : m_Data(std::move(data))
    {
        auto* blob = hb_blob_create(reinterpret_cast<const char*>(m_Data.Bytes->data()),
                                    unsigned(m_Data.Bytes->size()),
                                    HB_MEMORY_MODE_READONLY,
                                    nullptr,
                                    nullptr);
        m_Face     = hb_face_create(blob, m_Data.Settings.Face);
        hb_blob_destroy(blob);
        m_Font = hb_font_create(m_Face);
        hb_ot_font_set_funcs(m_Font);
        auto upem = hb_face_get_upem(m_Face);
        hb_font_set_scale(m_Font, int(upem), int(upem));
        hb_font_make_immutable(m_Font);
        try
        {
            m_Atlas = std::make_unique<GlyphAtlas>(*this, options);
        }
        catch (...)
        {
            hb_font_destroy(m_Font);
            hb_face_destroy(m_Face);
            throw;
        }
    }
    FontAsset::~FontAsset()
    {
        m_Atlas.reset();
        hb_font_destroy(m_Font);
        hb_face_destroy(m_Face);
    }
    uint32_t FontAsset::GlyphIndex(uint32_t cp) const
    {
        hb_codepoint_t g = 0;
        hb_font_get_nominal_glyph(m_Font, cp, &g);
        return g;
    }
    GlyphAtlas&                        FontAsset::Atlas() const { return *m_Atlas; }
    std::variant<FontData, AssetError> ReadFontData(std::shared_ptr<const std::vector<std::byte>> bytes,
                                                    FontSettings                                  settings)
    {
        if (!bytes || bytes->empty() || bytes->size() > 128 * 1024 * 1024 ||
            (settings.GlyphSize != 32 && settings.GlyphSize != 48 && settings.GlyphSize != 64) ||
            settings.PixelRange < 2 || settings.PixelRange > 16)
            return AssetError {AssetErrorCode::InvalidData, "Invalid font bytes/settings"};
        auto* blob  = hb_blob_create(reinterpret_cast<const char*>(bytes->data()),
                                     unsigned(bytes->size()),
                                     HB_MEMORY_MODE_READONLY,
                                     nullptr,
                                     nullptr);
        auto  count = hb_face_count(blob);
        hb_blob_destroy(blob);
        if (settings.Face >= count)
            return AssetError {AssetErrorCode::InvalidData, "Font collection face is out of range"};
        FontData d;
        d.Bytes    = std::move(bytes);
        d.Settings = std::move(settings);
        FontAsset a(d);
        if (!hb_face_get_glyph_count(a.GetFace()))
            return AssetError {AssetErrorCode::InvalidData, "Font has no glyphs"};
        auto& m      = d.Metrics;
        m.UnitsPerEm = hb_face_get_upem(a.GetFace());
        hb_font_extents_t e {};
        hb_font_get_h_extents(a.GetFont(), &e);
        m.Ascender        = float(e.ascender) / m.UnitsPerEm;
        m.Descender       = float(e.descender) / m.UnitsPerEm;
        m.LineGap         = float(e.line_gap) / m.UnitsPerEm;
        hb_position_t pos = 0;
        hb_ot_metrics_get_position(a.GetFont(), HB_OT_METRICS_TAG_UNDERLINE_OFFSET, &pos);
        m.UnderlinePosition = float(pos) / m.UnitsPerEm;
        hb_ot_metrics_get_position(a.GetFont(), HB_OT_METRICS_TAG_UNDERLINE_SIZE, &pos);
        m.UnderlineThickness = float(pos) / m.UnitsPerEm;
        return d;
    }
    namespace
    {
        struct Outline
        {
            msdfgen::Shape  Shape;
            msdfgen::Point2 Current, Start;
        };
        void Move(hb_draw_funcs_t*, void* p, hb_draw_state_t*, float x, float y, void*)
        {
            auto& o = *static_cast<Outline*>(p);
            o.Shape.addContour();
            o.Current = o.Start = {x, y};
        }
        void Line(hb_draw_funcs_t*, void* p, hb_draw_state_t*, float x, float y, void*)
        {
            auto&           o = *static_cast<Outline*>(p);
            msdfgen::Point2 to(x, y);
            if (to != o.Current)
                o.Shape.contours.back().addEdge(msdfgen::EdgeHolder(o.Current, to));
            o.Current = to;
        }
        void Quad(hb_draw_funcs_t*, void* p, hb_draw_state_t*, float x, float y, float tx, float ty, void*)
        {
            auto& o = *static_cast<Outline*>(p);
            o.Shape.contours.back().addEdge(msdfgen::EdgeHolder(o.Current, {x, y}, {tx, ty}));
            o.Current = {tx, ty};
        }
        void Cubic(hb_draw_funcs_t*,
                   void* p,
                   hb_draw_state_t*,
                   float x,
                   float y,
                   float x2,
                   float y2,
                   float tx,
                   float ty,
                   void*)
        {
            auto& o = *static_cast<Outline*>(p);
            o.Shape.contours.back().addEdge(msdfgen::EdgeHolder(o.Current, {x, y}, {x2, y2}, {tx, ty}));
            o.Current = {tx, ty};
        }
        void Close(hb_draw_funcs_t* f, void* p, hb_draw_state_t* s, void* u)
        {
            auto& o = *static_cast<Outline*>(p);
            Line(f, p, s, float(o.Start.x), float(o.Start.y), u);
        }
    } // namespace
    std::variant<GlyphBitmap, AssetError> GenerateGlyph(const FontAsset& a, uint32_t glyph)
    {
        if (glyph >= hb_face_get_glyph_count(a.GetFace()))
            return AssetError {AssetErrorCode::InvalidArgument, "Glyph index out of range"};
        Outline o;
        auto*   f = hb_draw_funcs_create();
        hb_draw_funcs_set_move_to_func(f, Move, nullptr, nullptr);
        hb_draw_funcs_set_line_to_func(f, Line, nullptr, nullptr);
        hb_draw_funcs_set_quadratic_to_func(f, Quad, nullptr, nullptr);
        hb_draw_funcs_set_cubic_to_func(f, Cubic, nullptr, nullptr);
        hb_draw_funcs_set_close_path_func(f, Close, nullptr, nullptr);
        hb_font_draw_glyph(a.GetFont(), glyph, f, &o);
        hb_draw_funcs_destroy(f);
        GlyphBitmap b;
        b.Glyph   = glyph;
        auto em   = double(hb_face_get_upem(a.GetFace()));
        b.Advance = float(hb_font_get_glyph_h_advance(a.GetFont(), glyph) / em);
        o.Shape.contours.erase(std::remove_if(o.Shape.contours.begin(),
                                              o.Shape.contours.end(),
                                              [](const auto& c) { return c.edges.empty(); }),
                               o.Shape.contours.end());
        if (o.Shape.contours.empty())
            return b;
        if (!o.Shape.validate())
            return AssetError {AssetErrorCode::InvalidData, "Invalid glyph outline"};
        o.Shape.normalize();
        o.Shape.orientContours();
        // Ink-trap coloring preserves sharp corners; default MSDF config enables error correction and overlap support.
        msdfgen::edgeColoringInkTrap(o.Shape, 3.0, 0);
        auto   bounds = o.Shape.getBounds();
        double scale  = a.GetData().Settings.GlyphSize / em;
        double pad    = a.GetData().Settings.PixelRange;
        double l = std::floor(bounds.l * scale) - pad, bt = std::floor(bounds.b * scale) - pad;
        b.Width  = uint32_t(std::ceil(bounds.r * scale) + pad - l);
        b.Height = uint32_t(std::ceil(bounds.t * scale) + pad - bt);
        if (b.Width > 1024 || b.Height > 1024)
            return AssetError {AssetErrorCode::InvalidData, "Glyph exceeds atlas page"};
        b.Plane = {float(l / a.GetData().Settings.GlyphSize),
                   float(bt / a.GetData().Settings.GlyphSize),
                   float((l + b.Width) / a.GetData().Settings.GlyphSize),
                   float((bt + b.Height) / a.GetData().Settings.GlyphSize)};
        msdfgen::Bitmap<float, 4> bitmap(int(b.Width), int(b.Height));
        msdfgen::generateMTSDF(bitmap,
                               o.Shape,
                               msdfgen::Projection({scale, scale}, {-l / scale, -bt / scale}),
                               msdfgen::Range(pad / scale));
        b.Pixels.resize(size_t(b.Width) * b.Height * 4);
        for (uint32_t y = 0; y < b.Height; ++y)
            for (uint32_t x = 0; x < b.Width; ++x)
                for (int c = 0; c < 4; ++c)
                    b.Pixels[(size_t(y) * b.Width + x) * 4 + c] =
                        uint8_t(std::lround(std::clamp(bitmap(int(x), int(y))[c], 0.f, 1.f) * 255));
        return b;
    }
} // namespace Kans
