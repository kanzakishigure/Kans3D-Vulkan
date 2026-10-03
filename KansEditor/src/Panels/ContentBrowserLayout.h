#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace Kans::ContentBrowserLayout
{
    struct Result
    {
        float IconSize;
        int   Columns;
    };

    inline float CardWidth(float iconSize)
    {
        if (iconSize <= 64.0f)
            return iconSize + 12.0f;
        if (iconSize <= 96.0f)
            return iconSize + 16.0f;
        if (iconSize <= 128.0f)
            return iconSize + 20.0f;
        return iconSize + 24.0f;
    }

    inline float CardHeight(float iconSize, float lineHeight)
    {
        if (iconSize <= 64.0f)
            return iconSize + std::max(32.0f, 15.0f + lineHeight);
        if (iconSize <= 96.0f)
            return iconSize + std::max(50.0f, 19.0f + 2.0f * lineHeight);
        if (iconSize <= 128.0f)
            return iconSize + std::max(66.0f, 23.0f + 3.0f * lineHeight);
        return iconSize + std::max(80.0f, 26.0f + 4.0f * lineHeight);
    }

    inline Result
    Calculate(float gridWidth, float gridHeight, float viewportWidth, float viewportHeight, float lineHeight)
    {
        constexpr std::array<float, 7> iconTiers   = {48.0f, 64.0f, 80.0f, 96.0f, 128.0f, 160.0f, 192.0f};
        constexpr float                columnGap   = 12.0f;
        constexpr float                rowPadding  = 28.0f; // Two 14 px ImGui cell margins.
        constexpr float                visibleRows = 1.5f;  // A full row and a visible next row.
        constexpr int                  maxColumns  = 16;

        // ImGui viewport dimensions are in UI units, so this cap responds to
        // both window resolution and detached platform windows.
        const float viewportLimit = std::min(viewportWidth, viewportHeight) / 6.0f;
        for (auto it = iconTiers.rbegin(); it != iconTiers.rend(); ++it)
        {
            const float iconSize = *it;
            if (iconSize > viewportLimit || visibleRows * (CardHeight(iconSize, lineHeight) + rowPadding) > gridHeight)
                continue;

            const int columns =
                static_cast<int>(std::floor((gridWidth + columnGap) / (CardWidth(iconSize) + columnGap)));
            if (columns >= 2)
                return {iconSize, std::min(columns, maxColumns)};
        }

        // Even a very short panel can still display the smallest icon tier.
        const float iconSize = iconTiers.front();
        const int   columns = static_cast<int>(std::floor((gridWidth + columnGap) / (CardWidth(iconSize) + columnGap)));
        return {iconSize, std::clamp(columns, 1, maxColumns)};
    }
} // namespace Kans::ContentBrowserLayout
