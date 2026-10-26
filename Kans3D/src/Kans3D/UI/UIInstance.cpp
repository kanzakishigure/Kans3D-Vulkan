#include "kspch.h"
#include "UIInstance.h"
#include <set>
#include <glm/gtc/matrix_transform.hpp>
#include "UICaret.h"
namespace Kans
{
    namespace
    {
        template<class F>
        void Visit(std::vector<UIRuntimeWidget>& widgets, F&& f)
        {
            for (auto& w : widgets)
            {
                f(w);
                Visit(w.Children, f);
            }
        }
        bool AtlasGeometryCurrent(TextLayout& layout)
        {
            // Inserting unrelated glyphs changes the atlas generation without moving existing glyphs.
            // Keep shaping cached when every ready glyph still occupies the same rectangle/page.
            for (const auto& q : layout.Glyphs)
            {
                if (q.Atlas->Generation() == q.AtlasGeneration)
                    continue;
                auto entry = q.Atlas->Find(q.Glyph);
                if (!q.Ready || !entry || entry->Page != q.Page)
                    return false;
                auto      r    = entry->Rect;
                float     page = float(GlyphAtlas::PageSize);
                glm::vec4 uv(
                    float(r.X) / page, float(r.Y + r.Height) / page, float(r.X + r.Width) / page, float(r.Y) / page);
                if (q.UV != uv)
                    return false;
            }
            for (auto& q : layout.Glyphs)
                q.AtlasGeneration = q.Atlas->Generation();
            return true;
        }
        void Sort(std::vector<UIRuntimeWidget>& widgets)
        {
            // YAML::Node assignment changes shared node contents. Sort indices and move-construct
            // into fresh storage so neither property bags nor runtime state alias another sibling.
            std::vector<size_t> order(widgets.size());
            for (size_t i = 0; i < order.size(); ++i)
                order[i] = i;
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return widgets[a].Data.SortOrder < widgets[b].Data.SortOrder;
            });
            std::vector<UIRuntimeWidget> sorted;
            sorted.reserve(widgets.size());
            for (auto i : order)
                sorted.push_back(std::move(widgets[i]));
            widgets = std::move(sorted);
            for (auto& w : widgets)
                Sort(w.Children);
        }
    } // namespace
    UIInstance::UIInstance(Ref<const UIAsset> asset, UIAssetResolver resolver) :
        m_Asset(std::move(asset)), m_Resolver(std::move(resolver))
    {
        if (m_Asset)
            m_ID = m_Asset->assetID;
    }
    UIInstance::UIInstance(AssetID asset, UIAssetResolver resolver) : m_ID(asset), m_Resolver(std::move(resolver)) {}
    std::variant<bool, AssetError> UIInstance::Refresh()
    {
        auto asset = m_Resolver && m_ID.IsValid() ? m_Resolver(m_ID) : m_Asset;
        if (!asset)
            return AssetError {AssetErrorCode::NotFound, "UI root asset unavailable"};
        bool changed = !m_Revision || asset != m_Asset;
        for (auto& [id, old] : m_Sources)
        {
            auto next = m_Resolver ? m_Resolver(id) : Ref<const UIAsset>();
            if (!next && asset != m_Asset)
                continue;
            if (!next)
                return AssetError {AssetErrorCode::NotFound, "Nested UI asset unavailable: " + id.ToString()};
            changed |= next != old;
        }
        if (!changed)
            return false;
        std::map<AssetID, Ref<const UIAsset>> sources;
        std::set<AssetID>                     active;
        if (m_ID.IsValid())
            active.insert(m_ID);
        size_t count = 0;
        std::function<std::vector<UIRuntimeWidget>(
            const std::vector<UIWidget>&, const std::string&, const std::string&, size_t)>
            expand;
        expand =
            [&](const std::vector<UIWidget>& nodes, const std::string& scope, const std::string& path, size_t depth) {
                if (depth > 128)
                    throw std::runtime_error("UI instance nesting limit exceeded");
                std::vector<UIRuntimeWidget> result;
                for (auto& node : nodes)
                {
                    if (++count > 100000)
                        throw std::runtime_error("Expanded UI widget limit exceeded");
                    UIRuntimeWidget w;
                    w.TreeOrder = result.size();
                    w.Data      = node;
                    w.Data.Children.clear();
                    w.Key      = scope + std::to_string(node.ID);
                    w.Path     = path.empty() ? node.Name : path + '/' + node.Name;
                    w.Children = expand(node.Children, scope, w.Path, depth + 1);
                    if (node.Type == UIWidgetType::UIInstance)
                    {
                        auto id = AssetID::FromString(UIProperty<std::string>(node, "UI", ""));
                        if (!active.insert(id).second)
                            throw std::runtime_error("UI instance cycle involving " + id.ToString());
                        auto nested = m_Resolver ? m_Resolver(id) : Ref<const UIAsset>();
                        if (!nested)
                            throw std::runtime_error("Nested UI asset unavailable: " + id.ToString());
                        sources[id]      = nested;
                        UIData data      = nested->GetData();
                        auto   overrides = node.Properties["Overrides"];
                        if (overrides)
                            for (auto p : overrides)
                            {
                                auto name = p.first.as<std::string>();
                                auto e    = std::find_if(
                                    data.Exposed.begin(), data.Exposed.end(), [&](auto& e) { return e.Name == name; });
                                if (e == data.Exposed.end())
                                    throw std::runtime_error("Unexposed UI override: " + name);
                                std::function<UIWidget*(std::vector<UIWidget>&)> find = [&](auto& list) -> UIWidget* {
                                    for (auto& n : list)
                                    {
                                        if (n.ID == e->WidgetID)
                                            return &n;
                                        if (auto p = find(n.Children))
                                            return p;
                                    }
                                    return nullptr;
                                };
                                auto target = find(data.Widgets);
                                if (!target)
                                    throw std::runtime_error("Exposed widget ID unavailable: " + name);
                                auto applied = UISetProperty(*target, e->Property, p.second);
                                if (auto err = std::get_if<AssetError>(&applied))
                                    throw std::runtime_error(err->Message);
                            }
                        auto children = expand(data.Widgets, w.Key + ':' + id.ToString() + ':', w.Path, depth + 1);
                        for (auto& c : children)
                        {
                            if (std::any_of(w.Children.begin(), w.Children.end(), [&](auto& old) {
                                    return old.Data.Name == c.Data.Name;
                                }))
                                throw std::runtime_error("Duplicate sibling name in UIInstance: " + c.Data.Name);
                            w.Children.push_back(std::move(c));
                        }
                        active.erase(id);
                    }
                    for (size_t child = 0; child < w.Children.size(); ++child)
                        w.Children[child].TreeOrder = child;
                    result.push_back(std::move(w));
                }
                return result;
            };
        try
        {
            auto valid = ValidateUI(asset->GetData());
            if (auto e = std::get_if<AssetError>(&valid))
                return *e;
            auto                                                    tree = expand(asset->GetData().Widgets, "", "", 0);
            std::unordered_map<std::string, const UIRuntimeWidget*> previous;
            Visit(m_Widgets, [&](auto& w) { previous[w.Key] = &w; });
            Visit(tree, [&](auto& w) {
                if (auto old = previous.find(w.Key); old != previous.end())
                {
                    const auto& state  = *old->second;
                    w.State            = YAML::Clone(state.State);
                    w.Caret            = state.Caret;
                    w.Anchor           = state.Anchor;
                    w.EditOriginal     = state.EditOriginal;
                    w.Composition      = state.Composition;
                    w.TextScroll       = state.TextScroll;
                    w.ScrollVelocity   = state.ScrollVelocity;
                    w.ElasticOffset    = state.ElasticOffset;
                    w.CaretTime        = state.CaretTime;
                    w.Editing          = state.Editing;
                    w.CaretVisible     = state.CaretVisible;
                    w.Tint             = state.Tint;
                    w.VisualState      = state.VisualState;
                    w.FadeFrom         = state.FadeFrom;
                    w.FadeTo           = state.FadeTo;
                    w.FadeTime         = state.FadeTime;
                    w.HasVisualTexture = state.HasVisualTexture;
                    w.VisualTexture    = state.VisualTexture;
                }
                if (auto changes = m_Changes.find(w.Key); changes != m_Changes.end())
                    for (auto& [p, v] : changes->second)
                    {
                        auto applied = UISetProperty(w.Data, p, v);
                        if (auto e = std::get_if<AssetError>(&applied))
                            throw std::runtime_error(e->Message);
                    }
            });
            Visit(tree, [](auto& w) {
                if (w.Data.Type == UIWidgetType::Toggle && !w.Data.Properties["IsOn"])
                    w.Data.Properties["IsOn"] = false;
                if (w.Data.Type == UIWidgetType::Slider && !w.Data.Properties["Value"])
                    w.Data.Properties["Value"] = 0.f;
                if (w.Data.Type == UIWidgetType::ScrollView && !w.Data.Properties["ScrollPosition"])
                    w.Data.Properties["ScrollPosition"] = UIEncode(glm::vec2(0));
                if (w.Data.Type == UIWidgetType::InputField && !w.Data.Properties["Text"])
                    w.Data.Properties["Text"] = "";
            });
            Sort(tree);
            m_Widgets = std::move(tree);
            m_Asset   = asset;
            m_Sources = std::move(sources);
            m_Dirty   = true;
            ++m_Revision;
            Visit(m_Widgets, [&](auto& w) {
                if (w.Data.Type == UIWidgetType::Slider)
                    SetValue(w.Path, YAML::Node(UIProperty<float>(w.Data, "Value", 0)));
                if (w.Data.Type == UIWidgetType::Toggle && UIProperty<bool>(w.Data, "IsOn", false))
                    SetValue(w.Path, YAML::Node(true));
            });
            return true;
        }
        catch (const std::exception& e)
        {
            return AssetError {AssetErrorCode::InvalidData, e.what()};
        }
    }
    void UIInstance::Update(glm::vec2 canvas, FontSystem* fonts, FontResolver fontsByID)
    {
        m_Stats = {};
        if (!m_Asset || canvas.x <= 0 || canvas.y <= 0)
            return;
        m_Dirty |= canvas != m_Canvas;
        m_Canvas      = canvas;
        const auto& d = m_Asset->GetData();
        m_PixelScale  = d.ScaleMode == UIScaleMode::ConstantPixelSize ?
                            1 :
                            std::pow(canvas.x / d.ReferenceResolution.x, 1 - d.Match) *
                                std::pow(canvas.y / d.ReferenceResolution.y, d.Match);
        auto scale    = m_PixelScale;
        if (!m_CustomCanvasRect)
            m_CanvasRoot.Data.Rect.Size = canvas;
        m_CanvasRoot.Size   = m_CanvasRoot.Data.Rect.Size;
        auto canvasPosition = m_CanvasRoot.Data.Rect.Position;
        m_CanvasRoot.Bounds = {canvasPosition, canvasPosition + m_CanvasRoot.Size};
        if (m_Dirty)
            Visit(m_Widgets, [](auto& w) { w.MeasureDirty = w.GroupDirty = true; });
        for (auto& w : m_Widgets)
            Measure(w, canvas, fonts, fontsByID);
        bool layoutAgain = false;
        std::function<void(
            std::vector<UIRuntimeWidget>&, glm::vec2, const glm::mat4&, glm::vec4, float, bool, bool, bool)>
            resolve;
        resolve = [&](auto&            nodes,
                      glm::vec2        parent,
                      const glm::mat4& matrix,
                      glm::vec4        clip,
                      float            opacity,
                      bool             visible,
                      bool             enabled,
                      bool             parentDirty) {
            for (auto& w : nodes)
            {
                if (w.MeasureDirty)
                {
                    auto oldPreferred = w.Preferred;
                    Measure(w, parent, fonts, fontsByID);
                    if (w.Preferred != oldPreferred)
                    {
                        Invalidate(w.Path, true);
                        w.MeasureDirty = false;
                        layoutAgain    = true;
                    }
                }
                const auto& r         = w.Data.Rect;
                bool        rectDirty = parentDirty || w.RectDirty;
                if (rectDirty)
                {
                    glm::vec2 pos, size;
                    for (int i = 0; i < 2; ++i)
                    {
                        if (r.AnchorsMin[i] == r.AnchorsMax[i])
                        {
                            size[i] = r.Size[i] * scale;
                            pos[i]  = parent[i] * r.AnchorsMin[i] + r.Position[i] * scale - size[i] * r.Pivot[i];
                        }
                        else
                        {
                            pos[i]  = parent[i] * r.AnchorsMin[i] + r.Offsets[i] * scale;
                            size[i] = std::max(0.f, parent[i] * r.AnchorsMax[i] - r.Offsets[i + 2] * scale - pos[i]);
                        }
                    }
                    if (w.Driven)
                    {
                        pos  = w.LayoutPosition;
                        size = w.LayoutSize;
                    }
                    pos += w.ExternalOffset;
                    if (UIProperty<bool>(w.Data, "FitWidth", false))
                        size.x = w.Preferred.x;
                    if (UIProperty<bool>(w.Data, "FitHeight", false))
                        size.y = w.Preferred.y;
                    if (size != w.Size)
                        w.GroupDirty = true;
                    if (size != w.Size)
                        w.LayoutDirty = true;
                    w.Size      = size;
                    auto pivot  = size * r.Pivot;
                    w.Transform = matrix * glm::translate(glm::mat4(1), glm::vec3(pos + pivot, 0)) *
                                  glm::rotate(glm::mat4(1), glm::radians(r.Rotation), glm::vec3(0, 0, 1)) *
                                  glm::scale(glm::mat4(1), glm::vec3(r.Scale, 1)) *
                                  glm::translate(glm::mat4(1), glm::vec3(-pivot, 0));
                    glm::vec2 min(std::numeric_limits<float>::max()), max(-std::numeric_limits<float>::max());
                    for (auto p : {glm::vec2(0), glm::vec2(size.x, 0), size, glm::vec2(0, size.y)})
                    {
                        auto v = glm::vec2(w.Transform * glm::vec4(p, 0, 1));
                        min    = glm::min(min, v);
                        max    = glm::max(max, v);
                    }
                    w.Bounds = {min, max};
                    ++m_Stats.RectsResolved;
                    w.RectDirty = false;
                }
                w.Clip    = clip;
                w.Visible = visible && w.Data.Visible;
                w.Enabled = enabled && w.Data.Enabled;
                w.Opacity = opacity * UIProperty<float>(w.Data, "Opacity", 1);
                if (w.Data.Type == UIWidgetType::Text || w.Data.Type == UIWidgetType::InputField)
                {
                    auto id   = AssetID::FromString(UIProperty<std::string>(w.Data, "Font", ""));
                    auto font = fontsByID && id.IsValid() ? fontsByID(id) : Ref<const FontAsset>();
                    if (w.LayoutDirty || rectDirty || font != w.Font || !AtlasGeometryCurrent(w.Text))
                    {
                        auto req  = UITextRequest(w.Data, w.Size, scale);
                        req.Font  = font;
                        req.Fonts = fonts;
                        if (w.Data.Type == UIWidgetType::InputField)
                        {
                            if (!UIProperty<bool>(w.Data, "Multiline", false))
                                req.Overflow = TextOverflow::Overflow;
                            auto original    = Layout(req);
                            w.EditBoundaries = {0, uint32_t(req.Text.size())};
                            for (auto c : original.Carets)
                                w.EditBoundaries.push_back(c.Byte);
                            std::sort(w.EditBoundaries.begin(), w.EditBoundaries.end());
                            w.EditBoundaries.erase(std::unique(w.EditBoundaries.begin(), w.EditBoundaries.end()),
                                                   w.EditBoundaries.end());
                            if (UIProperty<std::string>(w.Data, "ContentType", "Any") == "Password")
                            {
                                auto mask = UIProperty<std::string>(w.Data, "MaskCharacter", u8"●");
                                if (mask.empty())
                                    mask = "*";
                                req.Text.clear();
                                for (size_t i = 1; i < w.EditBoundaries.size(); ++i)
                                    req.Text += mask;
                                w.Text = Layout(req);
                                for (auto& c : w.Text.Carets)
                                    c.Byte = w.EditBoundaries[std::min(size_t(c.Byte / mask.size()),
                                                                       w.EditBoundaries.size() - 1)];
                            }
                            else
                                w.Text = std::move(original);
                            if (w.Text.Carets.empty())
                                w.Text.Carets.push_back({0, 0, {0, req.Size}});
                        }
                        else
                            w.Text = Layout(req);
                        w.Font        = font;
                        w.LayoutDirty = false;
                        ++m_Stats.TextsLaidOut;
                    }
                    else
                        w.Text.TouchGlyphs();
                }
                Arrange(w);
                if (w.Data.Type == UIWidgetType::InputField)
                {
                    auto caret     = UICaretPosition(w);
                    w.TextScroll.x = std::max(0.f, std::max(w.TextScroll.x, caret.x - w.Size.x + 2));
                    w.TextScroll.x = std::min(w.TextScroll.x, caret.x);
                    w.TextScroll.y = std::max(0.f, std::max(w.TextScroll.y, caret.y - w.Size.y));
                    w.TextScroll.y = std::min(w.TextScroll.y, std::max(0.f, caret.y - w.Text.SizeUsed));
                }
                auto childClip = w.Clip;
                if (UIProperty<bool>(w.Data, "ClipChildren", false) || w.Data.Type == UIWidgetType::ScrollView ||
                    w.Data.Type == UIWidgetType::InputField)
                    childClip = {glm::max(glm::vec2(w.Clip), glm::vec2(w.Bounds)),
                                 glm::min(glm::vec2(w.Clip.z, w.Clip.w), glm::vec2(w.Bounds.z, w.Bounds.w))};
                auto childMatrix = w.Transform;
                if (w.Data.Type == UIWidgetType::ScrollView)
                {
                    auto contentPath  = UIProperty<std::string>(w.Data, "Content", "Content");
                    auto viewportPath = UIProperty<std::string>(w.Data, "Viewport", "");
                    auto content      = MutableFind(w.Path + '/' + contentPath);
                    if (!content)
                        content = MutableFind(contentPath);
                    auto viewport = viewportPath.empty() ? nullptr : MutableFind(w.Path + '/' + viewportPath);
                    if (!viewport && !viewportPath.empty())
                        viewport = MutableFind(viewportPath);
                    if (viewport)
                        viewport->Data.Properties["ClipChildren"] = true;
                    if (content)
                    {
                        auto estimate = [&](const UIRuntimeWidget& node, glm::vec2 parentSize) {
                            auto size = node.Driven ? node.LayoutSize : node.Data.Rect.Size * scale;
                            if (!node.Driven)
                                for (int axis = 0; axis < 2; ++axis)
                                    if (node.Data.Rect.AnchorsMin[axis] != node.Data.Rect.AnchorsMax[axis])
                                        size[axis] = std::max(
                                            0.f,
                                            parentSize[axis] * (node.Data.Rect.AnchorsMax[axis] -
                                                                node.Data.Rect.AnchorsMin[axis]) -
                                                (node.Data.Rect.Offsets[axis] + node.Data.Rect.Offsets[axis + 2]) *
                                                    scale);
                            if (UIProperty<bool>(node.Data, "FitWidth", false))
                                size.x = node.Preferred.x;
                            if (UIProperty<bool>(node.Data, "FitHeight", false))
                                size.y = node.Preferred.y;
                            return size;
                        };
                        auto viewportSize = viewport ? estimate(*viewport, w.Size) : w.Size;
                        auto extent       = glm::max(glm::vec2(0), estimate(*content, viewportSize) - viewportSize);
                        auto fraction =
                            glm::clamp(UIVec2(w.Data.Properties["ScrollPosition"]), glm::vec2(0), glm::vec2(1));
                        auto offset = -fraction * extent + w.ElasticOffset;
                        if (offset != w.ScrollOffset)
                        {
                            w.ScrollOffset          = offset;
                            content->ExternalOffset = offset;
                            content->RectDirty      = true;
                        }
                    }
                }
                resolve(w.Children, w.Size, childMatrix, childClip, w.Opacity, w.Visible, w.Enabled, rectDirty);
            }
        };
        // Width allocation can change wrapped text's preferred height. Propagate that constraint
        // bottom-up before the final top-down pass; cached branches remain untouched.
        for (int pass = 0; pass < 4; ++pass)
        {
            layoutAgain = false;
            for (auto& root : m_Widgets)
                Measure(root, canvas, fonts, fontsByID);
            resolve(m_Widgets,
                    m_CanvasRoot.Size,
                    glm::translate(glm::mat4(1), glm::vec3(canvasPosition, 0)),
                    {0, 0, canvas.x, canvas.y},
                    UIProperty<float>(m_CanvasRoot.Data, "Opacity", 1),
                    m_CanvasRoot.Data.Visible,
                    m_CanvasRoot.Data.Enabled,
                    m_Dirty);
            m_Dirty = false;
            if (!layoutAgain)
                break;
        }
    }
    UIRuntimeWidget* UIInstance::MutableFind(const std::string& path)
    {
        if (path.empty())
            return &m_CanvasRoot;
        UIRuntimeWidget* found = nullptr;
        Visit(m_Widgets, [&](auto& w) {
            if (w.Path == path)
                found = &w;
        });
        return found;
    }
    const UIRuntimeWidget* UIInstance::Find(const std::string& path) const
    {
        return const_cast<UIInstance*>(this)->MutableFind(path);
    }
    YAML::Node* UIInstance::State(const std::string& path)
    {
        auto w = MutableFind(path);
        return w ? &w->State : nullptr;
    }
    bool UIInstance::SetProperty(const std::string& path, const std::string& p, const YAML::Node& v)
    {
        auto w = Find(path);
        if (w && ((p == "Value" && w->Data.Type == UIWidgetType::Slider) ||
                  (p == "IsOn" && w->Data.Type == UIWidgetType::Toggle) ||
                  (p == "ScrollPosition" && w->Data.Type == UIWidgetType::ScrollView)))
            return SetValue(path, v);
        return ApplyProperty(path, p, v);
    }
    bool UIInstance::ApplyProperty(const std::string& path, const std::string& p, const YAML::Node& v)
    {
        auto w = MutableFind(path);
        if (!w)
            return false;
        YAML::Node before;
        if (p == "Rect")
            before = UIEncode(w->Data.Rect);
        else if (p == "Visible")
            before = w->Data.Visible;
        else if (p == "Enabled")
            before = w->Data.Enabled;
        else
            before = w->Data.Properties[p];
        if (before && YAML::Dump(before) == YAML::Dump(v))
            return true;
        if (std::holds_alternative<AssetError>(UISetProperty(w->Data, p, v)))
            return false;
        if (path.empty())
        {
            if (p == "Rect")
                m_CustomCanvasRect = true;
            m_Dirty = true;
        }
        else
            m_Changes[w->Key][p] = YAML::Clone(v);
        const bool layoutProperty =
            p == "Rect" || p == "Visible" || p == "Text" || p == "Font" || p == "Size" || p == "MinimumSize" ||
            p == "Language" || p == "Overflow" || p == "HorizontalAlignment" || p == "VerticalAlignment" ||
            p == "LineSpacing" || p == "LetterSpacing" || p == "Layout" || p == "Padding" || p == "Spacing" ||
            p == "ChildAlignment" || p == "IgnoreLayout" || p == "CellSize" || p == "Constraint" ||
            p == "ConstraintCount" || p == "ControlWidth" || p == "ControlHeight" || p == "ForceExpandWidth" ||
            p == "ForceExpandHeight" || p == "FitWidth" || p == "FitHeight" || p == "MinWidth" || p == "MinHeight" ||
            p == "PreferredWidth" || p == "PreferredHeight" || p == "FlexibleWidth" || p == "FlexibleHeight" ||
            p == "Multiline" || p == "ContentType" || p == "MaskCharacter";
        if (layoutProperty)
            Invalidate(path, p != "Text");
        return true;
    }
    bool UIInstance::SetText(const std::string& p, const std::string& v)
    {
        auto w = Find(p);
        return w && (w->Data.Type == UIWidgetType::Text || w->Data.Type == UIWidgetType::InputField) &&
               SetProperty(p, "Text", YAML::Node(v));
    }
    bool UIInstance::SetColor(const std::string& p, glm::vec4 v)
    {
        auto w = Find(p);
        return w && SetProperty(p, w->Data.Type == UIWidgetType::Panel ? "BackgroundColor" : "Color", UIEncode(v));
    }
    bool UIInstance::SetVisible(const std::string& p, bool v) { return SetProperty(p, "Visible", YAML::Node(v)); }
    bool UIInstance::SetTexture(const std::string& p, AssetID v)
    {
        auto w = Find(p);
        return w && w->Data.Type == UIWidgetType::Image && SetProperty(p, "Texture", YAML::Node(v.ToString()));
    }
    bool UIInstance::SetRect(const std::string& p, const UIRect& v) { return SetProperty(p, "Rect", UIEncode(v)); }
} // namespace Kans
