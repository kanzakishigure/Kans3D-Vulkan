#include "kspch.h"
#include "FontProduct.h"
#include <cmath>
#include <cstring>
#include <set>
namespace Kans
{
    namespace
    {
        struct Writer
        {
            std::vector<std::byte> Bytes;
            void                   U(uint32_t v)
            {
                for (int i = 0; i < 4; ++i)
                    Bytes.push_back(std::byte((v >> (i * 8)) & 255));
            }
            void F(float f)
            {
                uint32_t u;
                std::memcpy(&u, &f, 4);
                U(u);
            }
            void S(const std::string& s)
            {
                U(uint32_t(s.size()));
                for (auto c : s)
                    Bytes.push_back(std::byte(uint8_t(c)));
            }
        };
        struct Reader
        {
            const std::byte* P;
            size_t           Remaining;
            void             Need(size_t n)
            {
                if (n > Remaining)
                    throw AssetError {AssetErrorCode::InvalidData, "Truncated Font Product"};
            }
            uint32_t U()
            {
                Need(4);
                uint32_t u = 0;
                for (int i = 0; i < 4; ++i)
                    u |= uint32_t(std::to_integer<uint8_t>(*P++)) << (8 * i);
                Remaining -= 4;
                return u;
            }
            float F()
            {
                auto  u = U();
                float f;
                std::memcpy(&f, &u, 4);
                if (!std::isfinite(f))
                    throw AssetError {AssetErrorCode::InvalidData, "Nonfinite Font metric"};
                return f;
            }
            std::string S()
            {
                auto n = U();
                Need(n);
                if (n > 65536)
                    throw AssetError {AssetErrorCode::InvalidData, "Oversized Font string"};
                std::string s(reinterpret_cast<const char*>(P), n);
                P += n;
                Remaining -= n;
                return s;
            }
        };
    } // namespace
    std::variant<std::vector<std::byte>, AssetError> EncodeFontProduct(const FontData& d)
    {
        if (!d.Bytes || d.Bytes->size() > 128 * 1024 * 1024 || d.Preloaded.size() > 65536 ||
            d.Settings.Fallbacks.size() > 64)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Font Product"};
        Writer w;
        w.U(0x544e464b);
        w.U(1);
        w.U(uint32_t(AssetType::Font)); // KFNT v1, little endian.
        const auto& s = d.Settings;
        w.U(s.Face);
        w.U(s.GlyphSize);
        w.U(s.PixelRange);
        w.S(s.Preload);
        w.S(s.Extra);
        w.U(uint32_t(s.Fallbacks.size()));
        for (const auto& id : s.Fallbacks)
            w.S(id.ToString());
        const auto& m = d.Metrics;
        w.U(m.UnitsPerEm);
        w.F(m.Ascender);
        w.F(m.Descender);
        w.F(m.LineGap);
        w.F(m.UnderlinePosition);
        w.F(m.UnderlineThickness);
        w.U(uint32_t(d.Bytes->size()));
        w.Bytes.insert(w.Bytes.end(), d.Bytes->begin(), d.Bytes->end());
        w.U(uint32_t(d.Preloaded.size()));
        for (const auto& g : d.Preloaded)
        {
            w.U(g.Glyph);
            w.U(g.Width);
            w.U(g.Height);
            w.F(g.Plane.Left);
            w.F(g.Plane.Bottom);
            w.F(g.Plane.Right);
            w.F(g.Plane.Top);
            w.F(g.Advance);
            w.U(uint32_t(g.Pixels.size()));
            for (auto p : g.Pixels)
                w.Bytes.push_back(std::byte(p));
        }
        auto valid = DecodeFontProduct(w.Bytes.data(), w.Bytes.size());
        if (auto* error = std::get_if<AssetError>(&valid))
            return *error;
        return std::move(w.Bytes);
    }
    std::variant<FontData, AssetError> DecodeFontProduct(const std::byte* bytes, size_t size)
    try
    {
        if (!bytes || size > FontProductMaximumBytes)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Font Product length"};
        Reader r {bytes, size};
        if (r.U() != 0x544e464b)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Font magic"};
        if (r.U() != 1)
            return AssetError {AssetErrorCode::UnsupportedVersion, "Unsupported Font Product version"};
        if (r.U() != uint32_t(AssetType::Font))
            return AssetError {AssetErrorCode::InvalidData, "Invalid Font type"};
        FontData d;
        auto&    s     = d.Settings;
        s.Face         = r.U();
        s.GlyphSize    = r.U();
        s.PixelRange   = r.U();
        s.Preload      = r.S();
        s.Extra        = r.S();
        auto fallbacks = r.U();
        if (fallbacks > 64 || (s.Preload != "none" && s.Preload != "ascii" && s.Preload != "latin1"))
            return AssetError {AssetErrorCode::InvalidData, "Invalid Font settings"};
        for (uint32_t i = 0; i < fallbacks; ++i)
        {
            auto id = AssetID::FromString(r.S());
            if (!id.IsValid())
                return AssetError {AssetErrorCode::InvalidData, "Invalid fallback Font ID"};
            s.Fallbacks.push_back(id);
        }
        auto& m              = d.Metrics;
        m.UnitsPerEm         = r.U();
        m.Ascender           = r.F();
        m.Descender          = r.F();
        m.LineGap            = r.F();
        m.UnderlinePosition  = r.F();
        m.UnderlineThickness = r.F();
        if (!m.UnitsPerEm || m.UnitsPerEm > 16384 || m.Ascender <= m.Descender || m.Ascender > 16 ||
            m.Descender < -16 || std::abs(m.LineGap) > 16 || std::abs(m.UnderlinePosition) > 16 ||
            m.UnderlineThickness < 0 || m.UnderlineThickness > 16)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Font metrics"};
        auto length = r.U();
        r.Need(length);
        if (!length || length > 128 * 1024 * 1024)
            return AssetError {AssetErrorCode::InvalidData, "Invalid Font payload"};
        d.Bytes = std::make_shared<const std::vector<std::byte>>(r.P, r.P + length);
        r.P += length;
        r.Remaining -= length;
        auto read = ReadFontData(d.Bytes, s);
        if (auto* e = std::get_if<AssetError>(&read))
            return *e;
        const auto& actual = std::get<FontData>(read).Metrics;
        if (m.UnitsPerEm != actual.UnitsPerEm || m.Ascender != actual.Ascender || m.Descender != actual.Descender ||
            m.LineGap != actual.LineGap || m.UnderlinePosition != actual.UnderlinePosition ||
            m.UnderlineThickness != actual.UnderlineThickness)
            return AssetError {AssetErrorCode::InvalidData, "Font metrics disagree with payload"};
        FontAsset a(std::get<FontData>(std::move(read)));
        auto      count = r.U();
        if (count > 65536 || count > r.Remaining / 36)
            return AssetError {AssetErrorCode::InvalidData, "Invalid preload count"};
        std::set<uint32_t> seen;
        for (uint32_t i = 0; i < count; ++i)
        {
            GlyphBitmap g;
            g.Glyph   = r.U();
            g.Width   = r.U();
            g.Height  = r.U();
            g.Plane   = {r.F(), r.F(), r.F(), r.F()};
            g.Advance = r.F();
            auto n    = r.U();
            if (!seen.insert(g.Glyph).second || g.Glyph >= hb_face_get_glyph_count(a.GetFace()) || g.Width > 1024 ||
                g.Height > 1024 || bool(g.Width) != bool(g.Height) || n != size_t(g.Width) * g.Height * 4 ||
                g.Plane.Right < g.Plane.Left || g.Plane.Top < g.Plane.Bottom || std::abs(g.Plane.Left) > 32 ||
                std::abs(g.Plane.Right) > 32 || std::abs(g.Plane.Bottom) > 32 || std::abs(g.Plane.Top) > 32 ||
                std::abs(g.Advance) > 32)
                return AssetError {AssetErrorCode::InvalidData, "Invalid preload glyph"};
            r.Need(n);
            g.Pixels.resize(n);
            if (n)
                std::memcpy(g.Pixels.data(), r.P, n);
            r.P += n;
            r.Remaining -= n;
            d.Preloaded.push_back(std::move(g));
        }
        if (r.Remaining)
            return AssetError {AssetErrorCode::InvalidData, "Trailing Font bytes"};
        return d;
    }
    catch (const AssetError& e)
    {
        return e;
    }
    catch (const std::bad_alloc&)
    {
        return AssetError {AssetErrorCode::InvalidData, "Font allocation failed"};
    }
} // namespace Kans
