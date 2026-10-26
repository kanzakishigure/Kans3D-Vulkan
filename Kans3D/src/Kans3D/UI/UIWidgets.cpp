#include "kspch.h"
#include <cmath>
#include <set>
#include "UIInstance.h"
namespace Kans
{
    YAML::Node UIInstance::Value(const std::string& path) const
    {
        auto w = Find(path);
        if (!w)
            return {};
        const char* key = w->Data.Type == UIWidgetType::Toggle     ? "IsOn" :
                          w->Data.Type == UIWidgetType::Slider     ? "Value" :
                          w->Data.Type == UIWidgetType::ScrollView ? "ScrollPosition" :
                                                                     "Text";
        return YAML::Clone(w->Data.Properties[key]);
    }
    bool UIInstance::SetValue(const std::string& path, const YAML::Node& value)
    try
    {
        auto w = MutableFind(path);
        if (!w)
            return false;
        if (w->Data.Type == UIWidgetType::Slider)
        {
            float min = UIProperty<float>(w->Data, "Min", 0), max = std::max(min, UIProperty<float>(w->Data, "Max", 1));
            float v = value.as<float>();
            if (!std::isfinite(v))
                return false;
            if (UIProperty<bool>(w->Data, "WholeNumbers", false))
                v = std::round(v);
            v = glm::clamp(v, min, max);
            if (!ApplyProperty(path, "Value", YAML::Node(v)))
                return false;
            float t        = max > min ? (v - min) / (max - min) : 0;
            auto  dir      = UIProperty<std::string>(w->Data, "Direction", "LeftToRight");
            bool  vertical = dir == "BottomToTop" || dir == "TopToBottom";
            bool  reverse  = dir == "RightToLeft" || dir == "BottomToTop";
            for (auto key : {"Fill", "Handle"})
            {
                auto name = UIProperty<std::string>(w->Data, key, "");
                if (name.empty())
                    continue;
                auto child = MutableFind(path + '/' + name);
                if (!child)
                    child = MutableFind(name);
                if (!child)
                    continue;
                auto r = child->Data.Rect;
                int  a = vertical ? 1 : 0;
                if (std::string(key) == "Handle")
                {
                    r.AnchorsMin[a] = r.AnchorsMax[a] = reverse ? 1 - t : t;
                    r.Position[a]                     = 0;
                }
                else
                {
                    r.AnchorsMin[a] = reverse ? 1 - t : 0;
                    r.AnchorsMax[a] = reverse ? 1 : t;
                    r.Offsets[a] = r.Offsets[a + 2] = 0;
                    r.Size[a]                       = 0;
                }
                SetRect(child->Path, r);
            }
            return true;
        }
        if (w->Data.Type == UIWidgetType::Toggle)
        {
            bool on = value.as<bool>();
            if (on)
            {
                auto group = UIProperty<std::string>(w->Data, "Group", "");
                std::function<void(const std::vector<UIRuntimeWidget>&)> visit = [&](auto& nodes) {
                    for (auto& c : nodes)
                    {
                        if (c.Path != path && c.Data.Type == UIWidgetType::Toggle && !group.empty() &&
                            UIProperty<std::string>(c.Data, "Group", "") == group)
                            ApplyProperty(c.Path, "IsOn", YAML::Node(false));
                        visit(c.Children);
                    }
                };
                visit(m_Widgets);
            }
            return ApplyProperty(path, "IsOn", YAML::Node(on));
        }
        if (w->Data.Type == UIWidgetType::ScrollView)
            return ApplyProperty(
                path, "ScrollPosition", UIEncode(glm::clamp(UIVec2(value), glm::vec2(0), glm::vec2(1))));
        return SetText(path, value.as<std::string>());
    }
    catch (const std::exception&)
    {
        return false;
    }
    bool UIInstance::AddChild(const std::string& path, UIWidget child)
    {
        UIData data;
        data.Widgets = {child};
        if (std::holds_alternative<AssetError>(ValidateUI(data)))
            return false;
        std::set<uint64_t>                                ids;
        std::function<void(const std::vector<UIWidget>&)> collect = [&](auto& nodes) {
            for (auto& w : nodes)
            {
                ids.insert(w.ID);
                collect(w.Children);
            }
        };
        collect(data.Widgets);
        bool                                                     duplicate = false;
        std::function<void(const std::vector<UIRuntimeWidget>&)> check     = [&](auto& nodes) {
            for (auto& w : nodes)
            {
                duplicate |= ids.count(w.Data.ID) != 0;
                check(w.Children);
            }
        };
        check(m_Widgets);
        if (duplicate)
            return false;
        auto parent = path.empty() ? nullptr : MutableFind(path);
        if (!path.empty() && !parent)
            return false;
        auto& nodes = parent ? parent->Children : m_Widgets;
        for (auto& c : nodes)
            if (c.Data.Name == child.Name)
                return false;
        UIInstance expanded(CreateRef<UIAsset>(data), m_Resolver);
        if (std::holds_alternative<AssetError>(expanded.Refresh()))
            return false;
        auto w      = std::move(expanded.m_Widgets.front());
        w.TreeOrder = 0;
        for (auto& c : nodes)
            w.TreeOrder = std::max(w.TreeOrder, c.TreeOrder + 1);
        std::function<void(UIRuntimeWidget&)> prefix = [&](auto& n) {
            if (!path.empty())
                n.Path = path + '/' + n.Path;
            n.Key = "runtime:" + path + ':' + n.Key;
            for (auto& c : n.Children)
                prefix(c);
        };
        prefix(w);
        nodes.push_back(std::move(w));
        if (parent)
            Invalidate(path);
        else
            m_Dirty = true;
        return true;
    }
    bool UIInstance::Remove(const std::string& path)
    {
        auto slash      = path.find_last_of('/');
        auto parentPath = slash == std::string::npos ? "" : path.substr(0, slash);
        auto parent     = parentPath.empty() ? nullptr : MutableFind(parentPath);
        if (!parentPath.empty() && !parent)
            return false;
        auto& nodes = parent ? parent->Children : m_Widgets;
        for (auto i = nodes.begin(); i != nodes.end(); ++i)
            if (i->Path == path)
            {
                // YAML nodes alias on assignment; move-construct survivors instead of vector::erase's shifting
                // assignments.
                std::vector<UIRuntimeWidget> survivors;
                survivors.reserve(nodes.size() - 1);
                for (auto at = nodes.begin(); at != nodes.end(); ++at)
                    if (at != i)
                        survivors.push_back(std::move(*at));
                nodes = std::move(survivors);
                if (parent)
                    Invalidate(parentPath);
                else
                    m_Dirty = true;
                return true;
            }
        return false;
    }
    void UIInstance::Tick(float seconds, const std::string& hover, const std::string& press, const std::string& focus)
    {
        std::function<void(std::vector<UIRuntimeWidget>&)> visit = [&](auto& nodes) {
            for (auto& w : nodes)
            {
                if (UIInteractive(w.Data.Type) || UIProperty<bool>(w.Data, "Interactable", false))
                {
                    auto state = !w.Enabled || !UIProperty<bool>(w.Data, "Interactable", UIInteractive(w.Data.Type)) ?
                                     "Disabled" :
                                 w.Path == press ? "Pressed" :
                                 w.Path == hover ? "Hovered" :
                                 w.Path == focus ? "Selected" :
                                                   "Normal";
                    const auto& p      = w.Data.Properties;
                    auto        states = p["States"];
                    auto        style  = states ? states[state] : YAML::Node();
                    auto        target = style && style["Color"] ? UIVec4(style["Color"]) : glm::vec4(1);
                    if (state != w.VisualState || target != w.FadeTo)
                    {
                        w.VisualState = state;
                        w.FadeFrom    = w.Tint;
                        w.FadeTo      = target;
                        w.FadeTime    = 0;
                    }
                    w.FadeTime += std::max(0.f, seconds);
                    float duration = UIProperty<float>(w.Data, "TransitionDuration", 0);
                    w.Tint = glm::mix(w.FadeFrom, w.FadeTo, duration > 0 ? std::min(1.f, w.FadeTime / duration) : 1.f);
                    YAML::Node texture = style ? style["Texture"] : YAML::Node();
                    w.HasVisualTexture = bool(texture);
                    w.VisualTexture    = AssetID::FromString(texture ? texture.as<std::string>() : "");
                }
                if (w.Data.Type == UIWidgetType::InputField)
                {
                    w.CaretTime += seconds;
                    w.CaretVisible   = w.Path == focus && std::fmod(w.CaretTime, 1.f) < .5f;
                    auto placeholder = UIProperty<std::string>(w.Data, "Placeholder", "");
                    if (!placeholder.empty())
                    {
                        auto child = MutableFind(w.Path + '/' + placeholder);
                        if (!child)
                            child = MutableFind(placeholder);
                        if (child)
                            child->Data.Visible = UIProperty<std::string>(w.Data, "Text", "").empty();
                    }
                }
                if (w.Data.Type == UIWidgetType::ScrollView && press != w.Path &&
                    press.compare(0, w.Path.size() + 1, w.Path + '/') != 0)
                    w.ElasticOffset *= std::exp(-12.f * seconds);
                if (w.Data.Type == UIWidgetType::ScrollView && glm::length(w.ScrollVelocity) > .01f &&
                    press != w.Path && press.compare(0, w.Path.size() + 1, w.Path + '/') != 0)
                {
                    SetValue(w.Path,
                             UIEncode(UIVec2(w.Data.Properties["ScrollPosition"]) + w.ScrollVelocity * seconds));
                    w.ScrollVelocity *= std::exp(-8.f * seconds);
                }
                visit(w.Children);
            }
        };
        visit(m_Widgets);
    }
} // namespace Kans
