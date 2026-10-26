#pragma once
#include "Kans3D/Asset/Asset.h"
#include "Kans3D/Text/TextComponent.h"
namespace Kans
{
    struct UIScreenComponent
    {
        AssetID UI;
        int     SortOrder = 0;
        bool    Visible   = true;
    };
    inline bool operator==(const UIScreenComponent& a, const UIScreenComponent& b)
    {
        return a.UI == b.UI && a.SortOrder == b.SortOrder && a.Visible == b.Visible;
    }
    inline bool operator!=(const UIScreenComponent& a, const UIScreenComponent& b) { return !(a == b); }
    struct UIWorldComponent
    {
        AssetID       UI;
        int           SortOrder = 0;
        bool          Visible   = true;
        glm::vec2     WorldSize {1.92f, 1.08f};
        TextBillboard Billboard   = TextBillboard::None;
        bool          AlwaysOnTop = false, DoubleSided = true;
    };
    inline bool operator==(const UIWorldComponent& a, const UIWorldComponent& b)
    {
        return a.UI == b.UI && a.WorldSize == b.WorldSize && a.Billboard == b.Billboard &&
               a.AlwaysOnTop == b.AlwaysOnTop && a.DoubleSided == b.DoubleSided && a.SortOrder == b.SortOrder &&
               a.Visible == b.Visible;
    }
    inline bool operator!=(const UIWorldComponent& a, const UIWorldComponent& b) { return !(a == b); }
} // namespace Kans
