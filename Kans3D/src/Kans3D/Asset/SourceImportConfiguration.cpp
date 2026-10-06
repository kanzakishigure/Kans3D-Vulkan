#include "kspch.h"
#include "SourceImportConfiguration.h"
#include <cstdint>

namespace Kans
{
    bool SourceImportConfiguration::IsPersistableText(const std::string& text) noexcept
    {
        // yaml-cpp replaces malformed UTF-8 and Unicode noncharacters during
        // emission. Reject them instead of silently changing authored settings.
        size_t position = 0;
        while (position < text.size())
        {
            const auto first = static_cast<unsigned char>(text[position++]);
            if (first < 0x80)
                continue;
            uint32_t code;
            uint32_t minimum;
            size_t   remaining;
            if (first >= 0xc2 && first <= 0xdf)
            {
                code      = first & 0x1f;
                minimum   = 0x80;
                remaining = 1;
            }
            else if (first >= 0xe0 && first <= 0xef)
            {
                code      = first & 0x0f;
                minimum   = 0x800;
                remaining = 2;
            }
            else if (first >= 0xf0 && first <= 0xf4)
            {
                code      = first & 0x07;
                minimum   = 0x10000;
                remaining = 3;
            }
            else
                return false;
            if (remaining > text.size() - position)
                return false;
            while (remaining--)
            {
                const auto next = static_cast<unsigned char>(text[position++]);
                if ((next & 0xc0) != 0x80)
                    return false;
                code = (code << 6) | (next & 0x3f);
            }
            if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) ||
                (code >= 0xfdd0 && code <= 0xfdef) || (code & 0xfffe) == 0xfffe)
                return false;
        }
        return true;
    }
} // namespace Kans
