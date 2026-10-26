#include "kspch.h"
#include "UIAsset.h"
#include <cmath>
#include <functional>
#include <set>
namespace Kans
{
    UIWidget::UIWidget(const UIWidget& w) :
        ID(w.ID), Name(w.Name), Type(w.Type), Visible(w.Visible), Enabled(w.Enabled), Rect(w.Rect),
        SortOrder(w.SortOrder), Properties(YAML::Clone(w.Properties)), Children(w.Children)
    {}
    UIWidget& UIWidget::operator=(const UIWidget& w)
    {
        if (this != &w)
        {
            ID        = w.ID;
            Name      = w.Name;
            Type      = w.Type;
            Visible   = w.Visible;
            Enabled   = w.Enabled;
            Rect      = w.Rect;
            SortOrder = w.SortOrder;
            Properties.reset(YAML::Clone(w.Properties));
            Children = w.Children;
        }
        return *this;
    }
    glm::vec2 UIVec2(const YAML::Node& n, glm::vec2 f)
    {
        if (!n)
            return f;
        if (!n.IsSequence() || n.size() != 2)
            throw std::runtime_error("Expected a vec2");
        return {n[0].as<float>(), n[1].as<float>()};
    }
    glm::vec4 UIVec4(const YAML::Node& n, glm::vec4 f)
    {
        if (!n)
            return f;
        if (!n.IsSequence() || n.size() != 4)
            throw std::runtime_error("Expected a vec4");
        return {n[0].as<float>(), n[1].as<float>(), n[2].as<float>(), n[3].as<float>()};
    }
    YAML::Node UIEncode(glm::vec2 v)
    {
        YAML::Node n;
        n.push_back(v.x);
        n.push_back(v.y);
        return n;
    }
    YAML::Node UIEncode(glm::vec4 v)
    {
        auto n = UIEncode(glm::vec2(v));
        n.push_back(v.z);
        n.push_back(v.w);
        return n;
    }
    YAML::Node UIEncode(const UIRect& r)
    {
        YAML::Node n;
        n["AnchorsMin"] = UIEncode(r.AnchorsMin);
        n["AnchorsMax"] = UIEncode(r.AnchorsMax);
        n["Pivot"]      = UIEncode(r.Pivot);
        n["Position"]   = UIEncode(r.Position);
        n["Size"]       = UIEncode(r.Size);
        n["Offsets"]    = UIEncode(r.Offsets);
        n["Rotation"]   = r.Rotation;
        n["Scale"]      = UIEncode(r.Scale);
        return n;
    }
    UIRect UIDecodeRect(const YAML::Node& n)
    {
        UIRect r;
        if (!n)
            return r;
        if (!n.IsMap())
            throw std::runtime_error("Rect must be a map");
        r.AnchorsMin = UIVec2(n["AnchorsMin"], r.AnchorsMin);
        r.AnchorsMax = UIVec2(n["AnchorsMax"], r.AnchorsMax);
        r.Pivot      = UIVec2(n["Pivot"], r.Pivot);
        r.Position   = UIVec2(n["Position"], r.Position);
        r.Size       = UIVec2(n["Size"], r.Size);
        r.Offsets    = UIVec4(n["Offsets"], r.Offsets);
        r.Scale      = UIVec2(n["Scale"], r.Scale);
        if (n["Rotation"])
            r.Rotation = n["Rotation"].as<float>();
        return r;
    }
    namespace
    {
        const char* Types[] =
            {"Panel", "Image", "Text", "UIInstance", "Button", "Toggle", "Slider", "ScrollView", "InputField"};
        bool Finite(glm::vec4 v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
        }
        void CheckRect(const UIRect& r)
        {
            if (!Finite({r.AnchorsMin, r.AnchorsMax}) || !Finite({r.Pivot, r.Position}) || !Finite({r.Size, r.Scale}) ||
                !Finite(r.Offsets) || !std::isfinite(r.Rotation) ||
                glm::any(glm::lessThan(r.AnchorsMin, glm::vec2(0))) ||
                glm::any(glm::greaterThan(r.AnchorsMax, glm::vec2(1))) ||
                glm::any(glm::greaterThan(r.AnchorsMin, r.AnchorsMax)) ||
                glm::any(glm::lessThan(r.Pivot, glm::vec2(0))) || glm::any(glm::greaterThan(r.Pivot, glm::vec2(1))) ||
                glm::any(glm::lessThan(r.Size, glm::vec2(0))))
                throw std::runtime_error("Invalid rect: anchor range, pivot, size or non-finite value");
        }
        UIWidget ReadWidget(const YAML::Node& n, size_t depth, size_t& count)
        {
            if (depth > 128 || ++count > 100000)
                throw std::runtime_error("UI widget tree limit exceeded");
            if (!n.IsMap())
                throw std::runtime_error("Widget must be a map");
            UIWidget w;
            w.ID        = n["ID"].as<uint64_t>();
            w.Name      = n["Name"].as<std::string>();
            auto   type = n["Type"].as<std::string>();
            size_t t    = 0;
            while (t < std::size(Types) && type != Types[t])
                ++t;
            if (t == std::size(Types))
                throw std::runtime_error("Unknown UI widget type: " + type);
            w.Type      = UIWidgetType(t);
            w.Visible   = n["Visible"].as<bool>(true);
            w.Enabled   = n["Enabled"].as<bool>(true);
            w.SortOrder = n["SortOrder"].as<int>(0);
            w.Rect      = UIDecodeRect(n["Rect"]);
            if (n["Properties"])
                w.Properties = YAML::Clone(n["Properties"]);
            if (n["Children"])
            {
                if (!n["Children"].IsSequence())
                    throw std::runtime_error("Children must be a sequence");
                for (auto c : n["Children"])
                    w.Children.push_back(ReadWidget(c, depth + 1, count));
            }
            return w;
        }
        YAML::Node WriteWidget(const UIWidget& w)
        {
            YAML::Node n;
            n["ID"]         = w.ID;
            n["Name"]       = w.Name;
            n["Type"]       = Types[int(w.Type)];
            n["Visible"]    = w.Visible;
            n["Enabled"]    = w.Enabled;
            n["Rect"]       = UIEncode(w.Rect);
            n["SortOrder"]  = w.SortOrder;
            n["Properties"] = YAML::Clone(w.Properties);
            n["Children"]   = YAML::Node(YAML::NodeType::Sequence);
            for (auto& c : w.Children)
                n["Children"].push_back(WriteWidget(c));
            return n;
        }
        void CheckProperty(const UIWidget& w, const std::string& p, const YAML::Node& v, bool strict)
        {
            auto type = w.Type;
            if (p == "Interactable" || p == "BlockPointer" || p == "IsOn" || p == "WholeNumbers" || p == "Horizontal" ||
                p == "Vertical" || p == "Inertia" || p == "Multiline" || p == "IgnoreLayout" || p == "ControlWidth" ||
                p == "ControlHeight" || p == "ForceExpandWidth" || p == "ForceExpandHeight" || p == "FitWidth" ||
                p == "FitHeight")
            {
                v.as<bool>();
                return;
            }
            if (p == "Min" || p == "Max" || p == "Value" || p == "Step" || p == "WheelSpeed" ||
                p == "TransitionDuration" || p == "Spacing" || p == "MinWidth" || p == "MinHeight" ||
                p == "PreferredWidth" || p == "PreferredHeight" || p == "FlexibleWidth" || p == "FlexibleHeight")
            {
                if (!std::isfinite(v.as<float>()))
                    throw std::runtime_error("Non-finite " + p);
                return;
            }
            if (p == "CharacterLimit" || p == "ConstraintCount")
            {
                if (v.as<int>() < 0)
                    throw std::runtime_error("Negative " + p);
                return;
            }
            if (p == "ScrollPosition" || p == "CellSize" || p == "ChildAlignment")
            {
                if (!Finite({UIVec2(v), 0, 0}))
                    throw std::runtime_error("Non-finite " + p);
                return;
            }
            if (p == "Padding")
            {
                if (!Finite(UIVec4(v)))
                    throw std::runtime_error("Non-finite padding");
                return;
            }
            if (p == "States")
            {
                if (!v.IsMap())
                    throw std::runtime_error("States must be a map");
                for (auto state : v)
                {
                    if (!state.second.IsMap())
                        throw std::runtime_error("Visual state must be a map");
                    if (state.second["Color"] && !Finite(UIVec4(state.second["Color"])))
                        throw std::runtime_error("Non-finite state color");
                    if (state.second["Texture"])
                        CheckProperty(w, "Texture", state.second["Texture"], true);
                }
                return;
            }
            if (p == "Group" || p == "NavUp" || p == "NavDown" || p == "NavLeft" || p == "NavRight" || p == "Fill" ||
                p == "Handle" || p == "Viewport" || p == "Content" || p == "Placeholder" || p == "MaskCharacter")
            {
                v.as<std::string>();
                return;
            }

            if (p == "TextColor" || p == "Color" || p == "BackgroundColor" || p == "UVRect" || p == "Borders" ||
                p == "OutlineColor" || p == "ShadowColor")
            {
                if (!Finite(UIVec4(v)))
                    throw std::runtime_error("Non-finite colour/vector");
                return;
            }
            if (p == "ShadowOffset")
            {
                if (!Finite({UIVec2(v), 0, 0}))
                    throw std::runtime_error("Non-finite offset");
                return;
            }
            if (p == "Opacity" || p == "Size" || p == "LineSpacing" || p == "LetterSpacing" || p == "OutlineWidth" ||
                p == "ShadowSoftness" || p == "MinimumSize")
            {
                auto f = v.as<float>();
                if (!std::isfinite(f) || (p != "LetterSpacing" && f < 0) || (p == "Opacity" && f > 1))
                    throw std::runtime_error("Invalid numeric property: " + p);
                return;
            }
            if (p == "ClipChildren" || p == "PreserveAspect")
            {
                v.as<bool>();
                return;
            }
            if (p == "Texture" || p == "Font" || p == "UI")
            {
                auto s = v.as<std::string>();
                if (!s.empty() && s != AssetID().ToString() && !AssetID::FromString(s).IsValid())
                    throw std::runtime_error("Invalid asset reference: " + p);
                return;
            }
            if (p == "Text" || p == "Language")
            {
                v.as<std::string>();
                return;
            }
            auto choice = [&](std::initializer_list<const char*> values) {
                auto s = v.as<std::string>();
                for (auto x : values)
                    if (s == x)
                        return;
                throw std::runtime_error("Invalid " + p + ": " + s);
            };
            if (p == "Direction")
            {
                choice({"LeftToRight", "RightToLeft", "BottomToTop", "TopToBottom"});
                return;
            }
            if (p == "Movement")
            {
                choice({"Clamped", "Elastic"});
                return;
            }
            if (p == "ContentType")
            {
                choice({"Any", "Integer", "Decimal", "Alphanumeric", "Password"});
                return;
            }
            if (p == "Layout")
            {
                choice({"None", "Horizontal", "Vertical", "Grid"});
                return;
            }
            if (p == "Constraint")
            {
                choice({"Columns", "Rows"});
                return;
            }
            if (p == "DrawMode")
            {
                choice({"Simple", "Sliced", "Tiled"});
                return;
            }
            if (p == "HorizontalAlignment")
            {
                choice({"Left", "Center", "Right"});
                return;
            }
            if (p == "VerticalAlignment")
            {
                choice({"Top", "Center", "Bottom"});
                return;
            }
            if (p == "Overflow")
            {
                choice({"Overflow", "Wrap", "Ellipsis", "ShrinkToFit"});
                return;
            }
            if (p == "Overrides" && type == UIWidgetType::UIInstance)
            {
                if (!v.IsMap())
                    throw std::runtime_error("Overrides must be a map");
                return;
            }
            if (strict)
                throw std::runtime_error("Unknown property: " + p);
        }
    } // namespace
    std::variant<std::monostate, AssetError> ValidateUI(const UIData& d)
    {
        try
        {
            if (!Finite({d.ReferenceResolution, d.Match, 0}) || d.ReferenceResolution.x <= 0 ||
                d.ReferenceResolution.y <= 0 || d.Match < 0 || d.Match > 1 || int(d.ScaleMode) > 1)
                throw std::runtime_error("Invalid UI reference resolution or scale mode/match");
            std::map<uint64_t, const UIWidget*>                       ids;
            size_t                                                    count = 0;
            std::function<void(const std::vector<UIWidget>&, size_t)> visit = [&](const std::vector<UIWidget>& nodes,
                                                                                  size_t                       depth) {
                std::set<std::string> names;
                if (depth > 128)
                    throw std::runtime_error("UI nesting limit exceeded");
                for (const auto& w : nodes)
                {
                    if (++count > 100000 || !w.ID || !ids.emplace(w.ID, &w).second)
                        throw std::runtime_error("Duplicate or invalid widget ID");
                    if (w.Name.empty() || w.Name.find('/') != std::string::npos || !names.insert(w.Name).second)
                        throw std::runtime_error("Duplicate sibling name or invalid widget name: " + w.Name);
                    if (int(w.Type) < 0 || int(w.Type) >= int(std::size(Types)))
                        throw std::runtime_error("Invalid widget type");
                    CheckRect(w.Rect);
                    if (!w.Properties.IsMap())
                        throw std::runtime_error("Properties must be a map");
                    for (auto p : w.Properties)
                        CheckProperty(w, p.first.as<std::string>(), p.second, false);
                    if (w.Type == UIWidgetType::UIInstance &&
                        !AssetID::FromString(UIProperty<std::string>(w, "UI", "")).IsValid())
                        throw std::runtime_error("UIInstance requires a UI asset");
                    visit(w.Children, depth + 1);
                }
            };
            visit(d.Widgets, 0);
            std::set<std::string> names;
            for (const auto& e : d.Exposed)
            {
                if (e.Name.empty() || !names.insert(e.Name).second || !ids.count(e.WidgetID))
                    throw std::runtime_error("Invalid or duplicate exposed property: " + e.Name);
                UIWidget copy(*ids.at(e.WidgetID));
                if (e.Property != "Visible" && e.Property != "Enabled" && e.Property != "Rect")
                {
                    if (!copy.Properties[e.Property])
                        throw std::runtime_error("Exposed property is absent: " + e.Property);
                    CheckProperty(copy, e.Property, copy.Properties[e.Property], true);
                }
            }
            return std::monostate {};
        }
        catch (const std::exception& e)
        {
            return AssetError {AssetErrorCode::InvalidData, std::string("UI: ") + e.what()};
        }
    }
    std::variant<UIData, AssetError> ParseUISource(const std::byte* bytes, size_t size)
    {
        try
        {
            if (!bytes || size > 16 * 1024 * 1024)
                throw std::runtime_error("Invalid source size");
            auto n = YAML::Load(std::string(reinterpret_cast<const char*>(bytes), size));
            if (!n.IsMap() || n["Version"].as<int>(1) != 1)
                throw std::runtime_error("Unsupported UI source version");
            UIData d;
            d.ReferenceResolution = UIVec2(n["ReferenceResolution"], d.ReferenceResolution);
            auto mode             = n["ScaleMode"].as<std::string>("ConstantPixelSize");
            if (mode == "ScaleWithScreenSize")
                d.ScaleMode = UIScaleMode::ScaleWithScreenSize;
            else if (mode != "ConstantPixelSize")
                throw std::runtime_error("Unknown scale mode");
            d.Match = n["Match"].as<float>(.5f);
            if (!n["Widgets"] || !n["Widgets"].IsSequence())
                throw std::runtime_error("Widgets must be a sequence");
            size_t count = 0;
            for (auto w : n["Widgets"])
                d.Widgets.push_back(ReadWidget(w, 0, count));
            if (n["Exposed"])
            {
                if (!n["Exposed"].IsSequence())
                    throw std::runtime_error("Exposed must be a sequence");
                for (auto e : n["Exposed"])
                    d.Exposed.push_back(
                        {e["Name"].as<std::string>(), e["WidgetID"].as<uint64_t>(), e["Property"].as<std::string>()});
            }
            auto valid = ValidateUI(d);
            if (auto e = std::get_if<AssetError>(&valid))
                return *e;
            return d;
        }
        catch (const std::exception& e)
        {
            return AssetError {AssetErrorCode::InvalidData, std::string("UI source: ") + e.what()};
        }
    }
    std::variant<std::string, AssetError> SerializeUISource(const UIData& d)
    {
        auto valid = ValidateUI(d);
        if (auto e = std::get_if<AssetError>(&valid))
            return *e;
        YAML::Node n;
        n["Version"]             = 1;
        n["ReferenceResolution"] = UIEncode(d.ReferenceResolution);
        n["ScaleMode"] = d.ScaleMode == UIScaleMode::ConstantPixelSize ? "ConstantPixelSize" : "ScaleWithScreenSize";
        n["Match"]     = d.Match;
        n["Widgets"]   = YAML::Node(YAML::NodeType::Sequence);
        for (auto& w : d.Widgets)
            n["Widgets"].push_back(WriteWidget(w));
        for (auto& e : d.Exposed)
        {
            YAML::Node p;
            p["Name"]     = e.Name;
            p["WidgetID"] = e.WidgetID;
            p["Property"] = e.Property;
            n["Exposed"].push_back(p);
        }
        return YAML::Dump(n);
    }
    std::map<AssetID, AssetType> UIDependencies(const UIData& d)
    {
        std::map<AssetID, AssetType>                      out;
        std::function<void(const std::vector<UIWidget>&)> visit = [&](const std::vector<UIWidget>& nodes) {
            for (auto& w : nodes)
            {
                for (const auto& entry : {std::pair<const char*, AssetType> {"Font", AssetType::Font},
                                          {"Texture", AssetType::Texture},
                                          {"UI", AssetType::UI}})
                {
                    auto id = AssetID::FromString(UIProperty<std::string>(w, entry.first, ""));
                    if (id.IsValid())
                        out[id] = entry.second;
                }
                if (auto states = w.Properties["States"])
                    for (auto state : states)
                    {
                        auto n  = state.second["Texture"];
                        auto id = AssetID::FromString(n ? n.as<std::string>() : "");
                        if (id.IsValid())
                            out[id] = AssetType::Texture;
                    }
                // Asset-valued overrides are dependencies too; exact target types are validated during expansion.
                if (w.Type == UIWidgetType::UIInstance && w.Properties["Overrides"])
                    for (auto p : w.Properties["Overrides"])
                        if (p.second.IsScalar())
                        {
                            auto id = AssetID::FromString(p.second.Scalar());
                            if (id.IsValid())
                                out.emplace(id, AssetType::None);
                        }
                visit(w.Children);
            }
        };
        visit(d.Widgets);
        return out;
    }
    std::variant<std::monostate, AssetError> UISetProperty(UIWidget& w, const std::string& p, const YAML::Node& v)
    {
        try
        {
            if (p == "Visible")
                w.Visible = v.as<bool>();
            else if (p == "Enabled")
                w.Enabled = v.as<bool>();
            else if (p == "Rect")
            {
                auto r = UIDecodeRect(v);
                CheckRect(r);
                w.Rect = r;
            }
            else
            {
                CheckProperty(w, p, v, true);
                w.Properties[p] = YAML::Clone(v);
            }
            return std::monostate {};
        }
        catch (const std::exception& e)
        {
            return AssetError {AssetErrorCode::InvalidData, std::string("UI property ") + p + ": " + e.what()};
        }
    }
    TextLayoutRequest UITextRequest(const UIWidget& w, glm::vec2 size, float s)
    {
        TextLayoutRequest r;
        r.Text          = UIProperty<std::string>(w, "Text", "");
        r.Language      = UIProperty<std::string>(w, "Language", "");
        r.Size          = UIProperty<float>(w, "Size", 16) * s;
        r.MinimumSize   = UIProperty<float>(w, "MinimumSize", 4) * s;
        r.WrapWidth     = size.x;
        r.BoxHeight     = size.y;
        r.LineSpacing   = UIProperty<float>(w, "LineSpacing", 1);
        r.LetterSpacing = UIProperty<float>(w, "LetterSpacing", 0);
        auto h          = UIProperty<std::string>(w, "HorizontalAlignment", "Left"),
             v          = UIProperty<std::string>(w, "VerticalAlignment", "Top"),
             o          = UIProperty<std::string>(w, "Overflow", "Wrap");
        r.Horizontal    = h == "Center" ? TextHorizontalAlignment::Center :
                          h == "Right"  ? TextHorizontalAlignment::Right :
                                          TextHorizontalAlignment::Left;
        r.Vertical      = v == "Center" ? TextVerticalAlignment::Center :
                          v == "Bottom" ? TextVerticalAlignment::Bottom :
                                          TextVerticalAlignment::Top;
        r.Overflow      = o == "Overflow"    ? TextOverflow::Overflow :
                          o == "Ellipsis"    ? TextOverflow::Ellipsis :
                          o == "ShrinkToFit" ? TextOverflow::ShrinkToFit :
                                               TextOverflow::Wrap;
        return r;
    }
    TextComponent UITextStyle(const UIWidget& w, float s)
    {
        auto          r = UITextRequest(w, {}, s);
        TextComponent c;
        c.Text     = r.Text;
        c.Language = r.Language;
        c.Size     = r.Size;
        c.Color    = UIVec4(w.Properties[w.Type == UIWidgetType::InputField ? "TextColor" : "Color"], glm::vec4(1));
        c.Outline.Width   = UIProperty<float>(w, "OutlineWidth", 0);
        c.Outline.Color   = UIVec4(w.Properties["OutlineColor"], {0, 0, 0, 1});
        c.Shadow.Color    = UIVec4(w.Properties["ShadowColor"], {});
        c.Shadow.Offset   = UIVec2(w.Properties["ShadowOffset"]);
        c.Shadow.Softness = UIProperty<float>(w, "ShadowSoftness", 0);
        return c;
    }
} // namespace Kans
