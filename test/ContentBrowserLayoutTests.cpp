#include <gtest/gtest.h>
#include "../KansEditor/src/Panels/ContentBrowserLayout.h"

using Kans::ContentBrowserLayout::Calculate;
using Kans::ContentBrowserLayout::CardWidth;

TEST(ContentBrowserLayout, CapsIconsToWindowResolution)
{
    const auto smallWindow = Calculate(1000.0f, 650.0f, 1360.0f, 768.0f, 17.0f);
    EXPECT_EQ(smallWindow.IconSize, 128.0f);
    EXPECT_EQ(smallWindow.Columns, 6);

    const auto largeWindow = Calculate(1000.0f, 650.0f, 2560.0f, 1440.0f, 17.0f);
    EXPECT_EQ(largeWindow.IconSize, 192.0f);
    EXPECT_EQ(largeWindow.Columns, 4);
}

TEST(ContentBrowserLayout, CapsIconsToPanelHeight)
{
    const auto shortPanel = Calculate(1000.0f, 300.0f, 1360.0f, 768.0f, 17.0f);
    EXPECT_EQ(shortPanel.IconSize, 96.0f);
    EXPECT_EQ(shortPanel.Columns, 8);

    const auto tinyPanel = Calculate(1000.0f, 60.0f, 1360.0f, 768.0f, 17.0f);
    EXPECT_EQ(tinyPanel.IconSize, 48.0f);
    EXPECT_GT(tinyPanel.Columns, 1);
}

TEST(ContentBrowserLayout, FitsCardsInAvailableGridWidth)
{
    for (const float width : {90.0f, 250.0f, 400.0f, 600.0f, 1000.0f})
    {
        const auto layout = Calculate(width, 700.0f, 2560.0f, 1440.0f, 17.0f);
        EXPECT_GE(layout.Columns, 1);
        EXPECT_LE(layout.Columns, 16);
        EXPECT_LE(layout.Columns * CardWidth(layout.IconSize) + (layout.Columns - 1) * 12.0f, width) << width;
    }
}
