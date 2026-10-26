#include "kspch.h"
#include <cmath>
#include "UIInstance.h"
namespace Kans
{
    namespace
    {
        std::string Group(const UIRuntimeWidget& w) { return UIProperty<std::string>(w.Data, "Layout", "None"); }
        bool        Included(const UIRuntimeWidget& w)
        {
            return w.Data.Visible && !UIProperty<bool>(w.Data, "IgnoreLayout", false);
        }
    } // namespace
    void UIInstance::Invalidate(const std::string& path, bool geometry)
    {
        std::function<bool(std::vector<UIRuntimeWidget>&)> mark = [&](auto& nodes) {
            for (auto& w : nodes)
            {
                if (w.Path == path)
                {
                    w.MeasureDirty = w.LayoutDirty = true;
                    w.GroupDirty |= geometry;
                    w.RectDirty |= geometry;
                    return true;
                }
                if (mark(w.Children))
                {
                    w.MeasureDirty = true;
                    if (geometry && (Group(w) != "None" || UIProperty<bool>(w.Data, "FitWidth", false) ||
                                     UIProperty<bool>(w.Data, "FitHeight", false)))
                        w.GroupDirty = w.RectDirty = true;
                    return true;
                }
            }
            return false;
        };
        mark(m_Widgets);
    }
    void UIInstance::Measure(UIRuntimeWidget& w, glm::vec2 available, FontSystem* fonts, const FontResolver& resolver)
    {
        w.MeasureDirty |= available != w.MeasureAvailable;
        w.MeasureAvailable = available;
        if (!w.MeasureDirty)
            return;
        ++m_Stats.MeasurePasses;
        auto oldPreferred = w.Preferred;
        auto s            = m_PixelScale;
        auto size         = w.Data.Rect.Size * s;
        for (int i = 0; i < 2; ++i)
            if (w.Data.Rect.AnchorsMin[i] != w.Data.Rect.AnchorsMax[i])
                size[i] = std::max(0.f,
                                   available[i] * (w.Data.Rect.AnchorsMax[i] - w.Data.Rect.AnchorsMin[i]) -
                                       (w.Data.Rect.Offsets[i] + w.Data.Rect.Offsets[i + 2]) * s);
        auto      group   = Group(w);
        auto      pad     = UIVec4(w.Data.Properties["Padding"]) * s;
        auto      spacing = UIProperty<float>(w.Data, "Spacing", 0) * s;
        auto      inner   = glm::max(glm::vec2(0), size - glm::vec2(pad.x + pad.z, pad.y + pad.w));
        glm::vec2 content(0);
        size_t    n = 0;
        for (auto& c : w.Children)
        {
            auto previous = c.Preferred;
            Measure(c, inner, fonts, resolver);
            if (previous != c.Preferred)
                w.GroupDirty = true;
            if (!Included(c))
                continue;
            if (group == "Horizontal")
            {
                content.x += c.Preferred.x;
                content.y = std::max(content.y, c.Preferred.y);
            }
            else if (group == "Vertical")
            {
                content.y += c.Preferred.y;
                content.x = std::max(content.x, c.Preferred.x);
            }
            else
                content = glm::max(content, c.Preferred + c.Data.Rect.Position * s);
            ++n;
        }
        if (n && (group == "Horizontal" || group == "Vertical"))
            content[group == "Horizontal" ? 0 : 1] += spacing * (n - 1);
        if (group == "Grid" && n)
        {
            int  constraint = std::max(1, UIProperty<int>(w.Data, "ConstraintCount", 1));
            bool rows       = UIProperty<std::string>(w.Data, "Constraint", "Columns") == "Rows";
            int  cols       = rows ? (int(n) + constraint - 1) / constraint : constraint;
            int  rowCount   = rows ? constraint : (int(n) + constraint - 1) / constraint;
            content         = UIVec2(w.Data.Properties["CellSize"], {100, 100}) * s * glm::vec2(cols, rowCount) +
                              spacing * glm::vec2(cols - 1, rowCount - 1);
        }
        if (w.Data.Type == UIWidgetType::Text || w.Data.Type == UIWidgetType::InputField)
        {
            auto req          = UITextRequest(w.Data, size, s);
            req.BoxHeight     = 0;
            req.Fonts         = fonts;
            auto id           = AssetID::FromString(UIProperty<std::string>(w.Data, "Font", ""));
            req.Font          = resolver && id.IsValid() ? resolver(id) : Ref<const FontAsset>();
            auto text         = Layout(req);
            content           = glm::vec2(text.Bounds.z - text.Bounds.x, text.Bounds.w - text.Bounds.y);
            const auto& state = w.State;
            float       width = state["MeasureWidth"].as<float>(-1);
            if (width >= 0 && width != req.WrapWidth)
            {
                req.WrapWidth    = width;
                auto constrained = Layout(req);
                content.y        = constrained.Bounds.w - constrained.Bounds.y;
            }
        }
        content += glm::vec2(pad.x + pad.z, pad.y + pad.w);
        w.Preferred = size;
        if (group != "None" || UIProperty<bool>(w.Data, "FitWidth", false) || w.Data.Type == UIWidgetType::Text)
            w.Preferred.x = content.x;
        if (group != "None" || UIProperty<bool>(w.Data, "FitHeight", false) || w.Data.Type == UIWidgetType::Text)
            w.Preferred.y = content.y;
        for (int i = 0; i < 2; ++i)
        {
            auto suffix = i == 0 ? "Width" : "Height";
            w.Preferred[i] =
                std::max(UIProperty<float>(w.Data, std::string("Min") + suffix, 0) * s,
                         UIProperty<float>(w.Data, std::string("Preferred") + suffix, w.Preferred[i] / s) * s);
        }
        if (oldPreferred != w.Preferred &&
            (UIProperty<bool>(w.Data, "FitWidth", false) || UIProperty<bool>(w.Data, "FitHeight", false)))
            w.RectDirty = true;
        w.MeasureDirty = false;
    }
    void UIInstance::Arrange(UIRuntimeWidget& w)
    {
        if (!w.GroupDirty)
            return;
        w.GroupDirty = false;
        auto group   = Group(w);
        if (group == "None")
            return;
        ++m_Stats.LayoutPasses;
        auto s     = m_PixelScale;
        auto p     = UIVec4(w.Data.Properties["Padding"]) * s;
        auto space = UIProperty<float>(w.Data, "Spacing", 0) * s;
        auto align = glm::clamp(UIVec2(w.Data.Properties["ChildAlignment"]), glm::vec2(0), glm::vec2(1));
        auto inner = glm::max(glm::vec2(0), w.Size - glm::vec2(p.x + p.z, p.y + p.w));
        std::vector<UIRuntimeWidget*> children;
        for (auto& c : w.Children)
        {
            if (Included(c))
                children.push_back(&c);
            else if (c.Driven)
            {
                c.Driven    = false;
                c.RectDirty = true;
            }
        }
        if (children.empty())
            return;
        auto drive = [&](UIRuntimeWidget& c, glm::vec2 pos, glm::vec2 size) {
            if (!c.Driven || pos != c.LayoutPosition || size != c.LayoutSize)
                c.RectDirty = c.GroupDirty = c.LayoutDirty = true;
            if (c.Data.Type == UIWidgetType::Text)
            {
                const auto& state = c.State;
                if (state["MeasureWidth"].as<float>(-1) != size.x)
                {
                    c.State["MeasureWidth"] = size.x;
                    c.MeasureDirty          = true;
                }
            }
            c.Driven         = true;
            c.LayoutPosition = pos;
            c.LayoutSize     = size;
        };
        if (group == "Grid")
        {
            auto cell   = UIVec2(w.Data.Properties["CellSize"], {100, 100}) * s;
            int  count  = std::max(1, UIProperty<int>(w.Data, "ConstraintCount", 1));
            bool rows   = UIProperty<std::string>(w.Data, "Constraint", "Columns") == "Rows";
            int  cols   = rows ? (int(children.size()) + count - 1) / count : count;
            int  height = rows ? count : (int(children.size()) + count - 1) / count;
            auto origin = glm::vec2(p) +
                          (inner - cell * glm::vec2(cols, height) - space * glm::vec2(cols - 1, height - 1)) * align;
            for (size_t i = 0; i < children.size(); ++i)
            {
                int x = rows ? int(i) / height : int(i) % cols, y = rows ? int(i) % height : int(i) / cols;
                drive(*children[i], origin + glm::vec2(x, y) * (cell + space), cell);
            }
            return;
        }
        int  axis = group == "Horizontal" ? 0 : 1, cross = 1 - axis;
        bool control[] = {UIProperty<bool>(w.Data, "ControlWidth", true),
                          UIProperty<bool>(w.Data, "ControlHeight", true)};
        bool expand[]  = {UIProperty<bool>(w.Data, "ForceExpandWidth", false),
                          UIProperty<bool>(w.Data, "ForceExpandHeight", false)};
        auto flex      = [&](const UIRuntimeWidget& c, int i) {
            auto value = std::max(0.f, UIProperty<float>(c.Data, i == 0 ? "FlexibleWidth" : "FlexibleHeight", 0));
            return expand[i] ? (value > 0 ? value : 1.f) : value;
        };
        float total = space * (children.size() - 1), weights = 0;
        for (auto c : children)
        {
            total += control[axis] ? c->Preferred[axis] : c->Data.Rect.Size[axis] * s;
            weights += flex(*c, axis);
        }
        float extra  = std::max(0.f, inner[axis] - total),
              cursor = (inner[axis] - total - (weights > 0 ? extra : 0)) * align[axis];
        for (auto c : children)
        {
            auto size = c->Data.Rect.Size * s;
            if (control[axis])
                size[axis] = c->Preferred[axis];
            if (control[axis] && weights > 0)
                size[axis] += extra * flex(*c, axis) / weights;
            if (control[cross])
                size[cross] = flex(*c, cross) > 0 ? std::max(c->Preferred[cross], inner[cross]) : c->Preferred[cross];
            glm::vec2 pos(p);
            pos[axis] += cursor;
            pos[cross] += (inner[cross] - size[cross]) * align[cross];
            drive(*c, pos, size);
            cursor += size[axis] + space;
        }
    }
} // namespace Kans
