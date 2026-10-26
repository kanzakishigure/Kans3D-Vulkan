#include "kspch.h"
#include <hb.h>
#include <GLFW/glfw3.h>
#include "UICaret.h"
#include "UIContext.h"
namespace Kans
{
    namespace
    {
        std::string UTF8(char32_t c)
        {
            std::string s;
            if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
                return s;
            if (c <= 0x7f)
                s += char(c);
            else if (c <= 0x7ff)
            {
                s += char(0xc0 | (c >> 6));
                s += char(0x80 | (c & 63));
            }
            else if (c <= 0xffff && !(c >= 0xd800 && c <= 0xdfff))
            {
                s += char(0xe0 | (c >> 12));
                s += char(0x80 | ((c >> 6) & 63));
                s += char(0x80 | (c & 63));
            }
            else if (c <= 0x10ffff)
            {
                s += char(0xf0 | (c >> 18));
                s += char(0x80 | ((c >> 12) & 63));
                s += char(0x80 | ((c >> 6) & 63));
                s += char(0x80 | (c & 63));
            }
            return s;
        }
        std::u32string Decode(const std::string& text)
        {
            std::u32string out;
            for (size_t at = 0; at < text.size();)
            {
                auto     b = static_cast<unsigned char>(text[at++]);
                char32_t c = b;
                int      n = b < 0x80               ? 0 :
                             b >= 0xc2 && b <= 0xdf ? 1 :
                             b <= 0xef && b >= 0xe0 ? 2 :
                             b <= 0xf4 && b >= 0xf0 ? 3 :
                                                      -1;
                if (n < 0 || at + n > text.size())
                    continue;
                bool valid = true;
                for (int i = 0; i < n; ++i)
                {
                    auto next = static_cast<unsigned char>(text[at + i]);
                    valid &= (next & 0xc0) == 0x80;
                    c = i == 0 ? (b & (0x7f >> (n + 1))) << 6 | (next & 63) : c << 6 | (next & 63);
                }
                at += n;
                if (valid && c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff) &&
                    (n == 0 || c >= (n == 1 ? 0x80u :
                                     n == 2 ? 0x800u :
                                              0x10000u)))
                    out += c;
            }
            return out;
        }
    } // namespace
    void UIContext::Edit(const UITarget& t, const UIInputState& input)
    {
        auto w = Resolve(t);
        if (!w)
            return;
        auto text    = UIProperty<std::string>(w->Data, "Text", "");
        auto old     = text;
        auto key     = [&](KeyCode code) { return input.KeyDown.count(code) != 0; };
        bool control = input.Control || input.Super;
        auto bounds  = w->EditBoundaries;
        if (bounds.empty())
            bounds = {0};
        bounds.push_back(uint32_t(text.size()));
        std::sort(bounds.begin(), bounds.end());
        bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
        auto snap = [&](uint32_t p) {
            auto it = std::upper_bound(bounds.begin(), bounds.end(), std::min(p, uint32_t(text.size())));
            return it == bounds.begin() ? 0u : *std::prev(it);
        };
        w->Caret  = snap(w->Caret);
        w->Anchor = snap(w->Anchor);
        auto move = [&](uint32_t byte) {
            w->State.remove("CaretLine");
            w->Caret = byte;
            if (!input.Shift)
                w->Anchor = byte;
            w->CaretTime = 0;
        };
        if (control && key(KeyCode::A))
        {
            w->Anchor = 0;
            w->Caret  = uint32_t(text.size());
        }
        uint32_t lo = std::min(w->Caret, w->Anchor), hi = std::max(w->Caret, w->Anchor);
        if (key(KeyCode::Left) || input.Left)
        {
            auto it = std::lower_bound(bounds.begin(), bounds.end(), w->Caret);
            move(!input.Shift && lo != hi ? lo : it == bounds.begin() ? 0 : *std::prev(it));
        }
        if (key(KeyCode::Right) || input.Right)
        {
            auto it = std::upper_bound(bounds.begin(), bounds.end(), w->Caret);
            move(!input.Shift && lo != hi ? hi : it == bounds.end() ? uint32_t(text.size()) : *it);
        }
        if (key(KeyCode::Home) || key(KeyCode::End))
        {
            uint32_t target     = key(KeyCode::Home) ? 0 : uint32_t(text.size());
            uint32_t visualLine = UICaretLine(*w);
            if (!control && UIProperty<bool>(w->Data, "Multiline", false))
            {
                std::vector<uint32_t> line;
                for (auto& c : w->Text.Carets)
                    if (c.Line == visualLine)
                        line.push_back(c.Byte);
                if (!line.empty())
                    target = key(KeyCode::Home) ? *std::min_element(line.begin(), line.end()) :
                                                  *std::max_element(line.begin(), line.end());
            }
            move(target);
            if (!control && UIProperty<bool>(w->Data, "Multiline", false))
                w->State["CaretLine"] = visualLine;
        }
        if (key(KeyCode::Up) || key(KeyCode::Down) || input.Up || input.Down)
        {
            auto p = UICaretPosition(*w);
            p.y += (key(KeyCode::Up) || input.Up ? -1.f : 1.f) * w->Text.SizeUsed;
            move(w->Text.NearestByteOffset(p));
            UIPlaceCaret(*w, p);
        }
        lo = std::min(w->Caret, w->Anchor);
        hi = std::max(w->Caret, w->Anchor);
        if (control && (key(KeyCode::C) || key(KeyCode::X)) && lo != hi)
        {
            auto selected = text.substr(lo, hi - lo);
            if (m_WriteClipboard)
                m_WriteClipboard(selected);
            else if (m_ClipboardWindow)
                glfwSetClipboardString(static_cast<GLFWwindow*>(m_ClipboardWindow), selected.c_str());
        }
        auto erase = [&] {
            text.erase(lo, hi - lo);
            w->Caret = w->Anchor = lo;
            hi                   = lo;
        };
        if (key(KeyCode::Backspace) || key(KeyCode::Delete) || (control && key(KeyCode::X)))
        {
            if (lo == hi && key(KeyCode::Backspace))
            {
                auto it = std::lower_bound(bounds.begin(), bounds.end(), lo);
                if (it != bounds.begin())
                    lo = *std::prev(it);
            }
            if (lo == hi && key(KeyCode::Delete))
            {
                auto it = std::upper_bound(bounds.begin(), bounds.end(), hi);
                if (it != bounds.end())
                    hi = *it;
            }
            erase();
        }
        auto typed = input.Typed;
        if (control && key(KeyCode::V))
        {
            std::string clipboard;
            if (m_ReadClipboard)
                clipboard = m_ReadClipboard();
            else if (m_ClipboardWindow)
            {
                auto s = glfwGetClipboardString(static_cast<GLFWwindow*>(m_ClipboardWindow));
                if (s)
                    clipboard = s;
            }
            typed += Decode(clipboard);
        }
        bool multiline = UIProperty<bool>(w->Data, "Multiline", false);
        bool enter     = input.Submit || key(KeyCode::Enter) || key(KeyCode::KPEnter);
        if (enter && multiline)
            typed += U'\n';
        if (!typed.empty())
        {
            lo        = std::min(w->Caret, w->Anchor);
            hi        = std::max(w->Caret, w->Anchor);
            auto base = text;
            base.erase(lo, hi - lo);
            auto        content = UIProperty<std::string>(w->Data, "ContentType", "Any");
            size_t      count   = Decode(base).size();
            int         limit   = UIProperty<int>(w->Data, "CharacterLimit", 0);
            std::string inserted;
            for (auto c : typed)
            {
                if ((c < 32 && !(multiline && (c == U'\n' || c == U'\t'))) || c == 127 ||
                    (limit > 0 && count >= size_t(limit)))
                    continue;
                auto candidate = base;
                candidate.insert(lo + inserted.size(), UTF8(c));
                bool valid = true;
                if (content == "Integer" || content == "Decimal")
                {
                    bool dot = false;
                    for (size_t j = 0; j < candidate.size(); ++j)
                    {
                        char ch = candidate[j];
                        if (ch >= '0' && ch <= '9')
                            continue;
                        if ((ch == '-' || ch == '+') && j == 0)
                            continue;
                        if (content == "Decimal" && ch == '.' && !dot)
                        {
                            dot = true;
                            continue;
                        }
                        valid = false;
                        break;
                    }
                }
                if (content == "Alphanumeric")
                {
                    auto category = hb_unicode_general_category(hb_unicode_funcs_get_default(), uint32_t(c));
                    valid         = (category >= HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER &&
                                     category <= HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER) ||
                                    category == HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER ||
                                    category == HB_UNICODE_GENERAL_CATEGORY_LETTER_NUMBER ||
                                    category == HB_UNICODE_GENERAL_CATEGORY_OTHER_NUMBER;
                }
                if (!valid)
                    continue;
                auto encoded = UTF8(c);
                if (encoded.empty())
                    continue;
                inserted += encoded;
                base = candidate;
                ++count;
            }
            if (!inserted.empty())
            {
                text     = base;
                w->Caret = w->Anchor = lo + uint32_t(inserted.size());
            }
        }
        if (input.Cancel || key(KeyCode::Escape))
        {
            text     = w->EditOriginal;
            w->Caret = w->Anchor = uint32_t(text.size());
            Emit(t, UIEventType::Cancelled);
        }
        if (text != old)
        {
            w->State.remove("CaretLine");
            SetValue(t, YAML::Node(text));
            w->CaretTime = 0;
        }
        if (enter && !multiline)
        {
            Emit(t, UIEventType::Submitted);
            w->EditOriginal = text;
        }
    }
} // namespace Kans
