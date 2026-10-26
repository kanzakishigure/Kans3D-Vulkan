#pragma once
#include <limits>
#include "UIInstance.h"
namespace Kans
{
    // A soft wrap has two visual positions at the same source byte. Keep affinity in runtime state.
    inline glm::vec2 UICaretPosition(const UIRuntimeWidget& w)
    {
        const auto& state = w.State;
        auto        line  = state["CaretLine"].as<uint32_t>(std::numeric_limits<uint32_t>::max());
        for (auto& caret : w.Text.Carets)
            if (caret.Byte == w.Caret && caret.Line == line)
                return caret.Position;
        return w.Text.CaretPosition(w.Caret);
    }
    inline uint32_t UICaretLine(const UIRuntimeWidget& w)
    {
        auto p = UICaretPosition(w);
        for (auto& caret : w.Text.Carets)
            if (caret.Byte == w.Caret && caret.Position == p)
                return caret.Line;
        return 0;
    }
    inline void UIPlaceCaret(UIRuntimeWidget& w, glm::vec2 point)
    {
        w.Caret       = w.Text.NearestByteOffset(point);
        float    best = std::numeric_limits<float>::max();
        uint32_t line = 0;
        for (auto& caret : w.Text.Carets)
        {
            if (caret.Byte != w.Caret)
                continue;
            float top = caret.Position.y - w.Text.SizeUsed, bottom = caret.Position.y;
            if (caret.Line < w.Text.Lines.size())
            {
                auto& l = w.Text.Lines[caret.Line];
                top     = l.BaselineY - l.Ascent;
                bottom  = l.BaselineY + l.Descent;
            }
            float distance = point.y < top ? top - point.y : point.y > bottom ? point.y - bottom : 0;
            if (distance < best)
            {
                best = distance;
                line = caret.Line;
            }
        }
        w.State["CaretLine"] = line;
    }
} // namespace Kans
