#include "kspch.h"
#include "UIInstance.h"
#include <set>
#include <glm/gtc/matrix_transform.hpp>
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
                    w.Data = node;
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
                    result.push_back(std::move(w));
                }
                return result;
            };
        try
        {
            auto valid = ValidateUI(asset->GetData());
            if (auto e = std::get_if<AssetError>(&valid))
                return *e;
            auto                                        tree = expand(asset->GetData().Widgets, "", "", 0);
            std::unordered_map<std::string, YAML::Node> states;
            Visit(m_Widgets, [&](auto& w) { states[w.Key] = YAML::Clone(w.State); });
            Visit(tree, [&](auto& w) {
                if (auto s = states.find(w.Key); s != states.end())
                    w.State = s->second;
                if (auto changes = m_Changes.find(w.Key); changes != m_Changes.end())
                    for (auto& [p, v] : changes->second)
                    {
                        auto applied = UISetProperty(w.Data, p, v);
                        if (auto e = std::get_if<AssetError>(&applied))
                            throw std::runtime_error(e->Message);
                    }
            });
            Sort(tree);
            m_Widgets = std::move(tree);
            m_Asset   = asset;
            m_Sources = std::move(sources);
            m_Dirty   = true;
            ++m_Revision;
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
        std::function<void(std::vector<UIRuntimeWidget>&, glm::vec2, const glm::mat4&, glm::vec4, float, bool, bool)>
            resolve;
        resolve = [&](auto&            nodes,
                      glm::vec2        parent,
                      const glm::mat4& matrix,
                      glm::vec4        clip,
                      float            opacity,
                      bool             visible,
                      bool             enabled) {
            for (auto& w : nodes)
            {
                const auto& r = w.Data.Rect;
                if (m_Dirty)
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
                }
                w.Clip    = clip;
                w.Visible = visible && w.Data.Visible;
                w.Enabled = enabled && w.Data.Enabled;
                w.Opacity =
                    opacity * (w.Data.Type == UIWidgetType::Panel ? UIProperty<float>(w.Data, "Opacity", 1) : 1);
                if (w.Data.Type == UIWidgetType::Text)
                {
                    auto id   = AssetID::FromString(UIProperty<std::string>(w.Data, "Font", ""));
                    auto font = fontsByID && id.IsValid() ? fontsByID(id) : Ref<const FontAsset>();
                    if (w.LayoutDirty || m_Dirty || font != w.Font || !AtlasGeometryCurrent(w.Text))
                    {
                        auto req      = UITextRequest(w.Data, w.Size, scale);
                        req.Font      = font;
                        req.Fonts     = fonts;
                        w.Text        = Layout(req);
                        w.Font        = font;
                        w.LayoutDirty = false;
                        ++m_Stats.TextsLaidOut;
                    }
                    else
                        w.Text.TouchGlyphs();
                }
                auto childClip = w.Clip;
                if (w.Data.Type == UIWidgetType::Panel && UIProperty<bool>(w.Data, "ClipChildren", false))
                    childClip = {glm::max(glm::vec2(w.Clip), glm::vec2(w.Bounds)),
                                 glm::min(glm::vec2(w.Clip.z, w.Clip.w), glm::vec2(w.Bounds.z, w.Bounds.w))};
                resolve(w.Children, w.Size, w.Transform, childClip, w.Opacity, w.Visible, w.Enabled);
            }
        };
        resolve(m_Widgets, canvas, glm::mat4(1), {0, 0, canvas.x, canvas.y}, 1, true, true);
        m_Dirty = false;
    }
    UIRuntimeWidget* UIInstance::MutableFind(const std::string& path)
    {
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
        m_Changes[w->Key][p] = YAML::Clone(v);
        if (p == "Rect")
            m_Dirty = true;
        else if (p != "Visible" && p != "Enabled" && p != "Color" && p != "Texture")
            w->LayoutDirty = true;
        return true;
    }
    bool UIInstance::SetText(const std::string& p, const std::string& v)
    {
        auto w = Find(p);
        return w && w->Data.Type == UIWidgetType::Text && SetProperty(p, "Text", YAML::Node(v));
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
