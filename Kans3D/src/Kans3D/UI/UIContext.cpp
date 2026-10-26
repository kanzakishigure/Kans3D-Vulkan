#include "kspch.h"
#include "UIContext.h"
#include <cmath>
#include <GLFW/glfw3.h>
#include "Kans3D/Renderer/RenderView.h"
#include "UICaret.h"
namespace Kans
{
    namespace
    {
        bool Inside(glm::vec2 p, glm::vec4 r) { return p.x >= r.x && p.y >= r.y && p.x < r.z && p.y < r.w; }
        bool Selectable(const UIRuntimeWidget& w)
        {
            return w.Visible && w.Enabled && UIProperty<bool>(w.Data, "Interactable", UIInteractive(w.Data.Type));
        }
        glm::vec2 Local(const UIRuntimeWidget& w, glm::vec2 p)
        {
            return glm::vec2(glm::inverse(w.Transform) * glm::vec4(p, 0, 1));
        }
        UITarget Target(const ViewUI& ui, const UIRuntimeWidget& w)
        {
            return {ui.Instance, w.Path, w.Key, ui.EntityID, ui.Screen};
        }
    } // namespace
    UIRuntimeWidget* UIContext::Resolve(const UITarget& t) const
    {
        auto i = t.Instance.lock();
        auto w = i ? i->MutableFind(t.Path) : nullptr;
        return w && w->Key == t.Key ? w : nullptr;
    }
    void UIContext::Emit(const UITarget& t, UIEventType type, glm::vec2 p, glm::vec2 delta)
    {
        if (!t)
            return;
        UIEvent e {t, type, p, delta};
        if (auto i = t.Instance.lock())
            e.Value = i->Value(t.Path);
        m_Events.push_back(std::move(e));
    }
    void UIContext::Reset()
    {
        m_Focus = {};
        m_Hover = {};
        m_Press = {};
        m_Events.clear();
        m_Dragging = false;
    }
    bool UIContext::SetFocus(const UITarget& t)
    {
        if (t)
        {
            auto w = Resolve(t);
            if (!w || !Selectable(*w))
                return false;
        }
        if (t == m_Focus)
            return true;
        if (auto old = Resolve(m_Focus))
            old->Editing = false;
        Emit(m_Focus, UIEventType::Blurred);
        m_Focus = t;
        if (auto w = Resolve(t))
        {
            w->EditOriginal = UIProperty<std::string>(w->Data, "Text", "");
            w->Editing      = true;
            w->CaretTime    = 0;
        }
        Emit(t, UIEventType::Focused);
        return true;
    }
    bool UIContext::SetValue(const UITarget& t, const YAML::Node& value)
    {
        auto i = t.Instance.lock();
        auto w = Resolve(t);
        if (!i || !w)
            return false;
        std::vector<std::pair<UITarget, std::string>>            before;
        std::function<void(const std::vector<UIRuntimeWidget>&)> visit = [&](auto& nodes) {
            for (auto& c : nodes)
            {
                auto target = t;
                target.Path = c.Path;
                target.Key  = c.Key;
                if (c.Path == t.Path || c.Data.Type == UIWidgetType::Toggle)
                    before.push_back({target, YAML::Dump(i->Value(c.Path))});
                visit(c.Children);
            }
        };
        visit(i->Widgets());
        if (!i->SetValue(t.Path, value))
            return false;
        for (auto& [target, old] : before)
            if (old != YAML::Dump(i->Value(target.Path)))
                Emit(target, UIEventType::Changed);
        return true;
    }
    void UIContext::Activate(const UITarget& t)
    {
        auto w = Resolve(t);
        if (!w || !Selectable(*w))
            return;
        Emit(t, UIEventType::Clicked, glm::vec2(w->Transform * glm::vec4(w->Size * .5f, 0, 1)));
        if (w->Data.Type == UIWidgetType::Toggle)
        {
            bool grouped = !UIProperty<std::string>(w->Data, "Group", "").empty();
            SetValue(t, YAML::Node(grouped || !UIProperty<bool>(w->Data, "IsOn", false)));
        }
    }
    UITarget UIContext::Ancestor(const UITarget& t, UIWidgetType type) const
    {
        auto out = t;
        auto i   = t.Instance.lock();
        if (!i)
            return {};
        while (true)
        {
            auto w = i->Find(out.Path);
            if (w && w->Data.Type == type)
            {
                out.Key = w->Key;
                return out;
            }
            auto slash = out.Path.find_last_of('/');
            if (slash == std::string::npos)
                return {};
            out.Path.resize(slash);
        }
    }
    void UIContext::Slider(const UITarget& t, glm::vec2 p)
    {
        auto w = Resolve(t);
        if (!w || w->Data.Type != UIWidgetType::Slider)
            return;
        auto  local    = Local(*w, p);
        auto  dir      = UIProperty<std::string>(w->Data, "Direction", "LeftToRight");
        int   axis     = dir == "BottomToTop" || dir == "TopToBottom" ? 1 : 0;
        float fraction = w->Size[axis] > 0 ? local[axis] / w->Size[axis] : 0;
        if (dir == "RightToLeft" || dir == "BottomToTop")
            fraction = 1 - fraction;
        float min = UIProperty<float>(w->Data, "Min", 0), max = UIProperty<float>(w->Data, "Max", 1);
        SetValue(t, YAML::Node(min + fraction * (max - min)));
    }
    void UIContext::Scroll(const UITarget& t, glm::vec2 delta, bool dragging, float seconds)
    {
        auto w = Resolve(t);
        if (!w)
            return;
        auto i       = t.Instance.lock();
        auto name    = UIProperty<std::string>(w->Data, "Content", "Content");
        auto content = i->Find(w->Path + '/' + name);
        if (!content)
            content = i->Find(name);
        if (!content)
            return;
        auto viewportPath = UIProperty<std::string>(w->Data, "Viewport", "");
        auto viewport     = i->Find(w->Path + '/' + viewportPath);
        if (!viewport)
            viewport = i->Find(viewportPath);
        auto extent = glm::max(glm::vec2(1), content->Size - (viewport ? viewport->Size : w->Size));
        auto d      = -delta * (dragging ? 1.f : UIProperty<float>(w->Data, "WheelSpeed", 30)) / extent;
        if (!UIProperty<bool>(w->Data, "Horizontal", false))
            d.x = 0;
        if (!UIProperty<bool>(w->Data, "Vertical", true))
            d.y = 0;
        auto next = UIVec2(w->Data.Properties["ScrollPosition"]) + d;
        if (dragging && UIProperty<std::string>(w->Data, "Movement", "Clamped") == "Elastic")
            w->ElasticOffset += (glm::clamp(next, glm::vec2(0), glm::vec2(1)) - next) * extent * .4f;
        SetValue(t, UIEncode(next));
        if (dragging && UIProperty<bool>(w->Data, "Inertia", false) && seconds > 0)
            w->ScrollVelocity = d / seconds;
    }
    void UIContext::PlaceCaret(const UITarget& t, glm::vec2 p, bool extend)
    {
        auto w = Resolve(t);
        if (!w || w->Data.Type != UIWidgetType::InputField)
            return;
        UIPlaceCaret(*w, Local(*w, p) + w->TextScroll);
        if (!extend)
            w->Anchor = w->Caret;
        w->CaretTime = 0;
    }
    void UIContext::Process(const UIInputState&        input,
                            const std::vector<ViewUI>& uis,
                            glm::vec2                  pixels,
                            const glm::mat4&           vp,
                            float                      seconds)
    {
        m_Events.clear();
        std::vector<const ViewUI*> order;
        for (auto& ui : uis)
        {
            if (!ui.Instance || !ui.Instance->Data())
                continue;
            ui.Instance->Update(ui.Screen ? pixels : ui.Instance->Data()->ReferenceResolution, m_Fonts, m_FontResolver);
            order.push_back(&ui);
        }
        auto live = [&](const UITarget& t) {
            auto w = Resolve(t);
            if (!w || !Selectable(*w))
                return false;
            return std::any_of(order.begin(), order.end(), [&](auto ui) {
                return ui->Instance == t.Instance.lock() && ui->Screen == t.Screen && ui->EntityID == t.Entity;
            });
        };
        if (m_Focus && !live(m_Focus))
            SetFocus({});
        if (m_Press && !live(m_Press))
        {
            if (m_Dragging)
                Emit(m_Press, UIEventType::DragEnd);
            m_Press    = {};
            m_Dragging = false;
        }
        auto      inverse = glm::inverse(vp);
        glm::vec2 ndc     = pixels.x > 0 && pixels.y > 0 ?
                                glm::vec2(2 * input.Pointer.x / pixels.x - 1, 1 - 2 * input.Pointer.y / pixels.y) :
                                glm::vec2(0);
        auto      near4 = inverse * glm::vec4(ndc, -1, 1), far4 = inverse * glm::vec4(ndc, 1, 1);
        glm::vec3 origin = glm::vec3(near4) / near4.w, ray = glm::normalize(glm::vec3(far4) / far4.w - origin);
        auto      point = [&](const ViewUI& ui, glm::vec2& p, float& distance) {
            if (ui.Screen)
            {
                p        = input.Pointer;
                distance = 0;
                return true;
            }
            if (std::abs(glm::determinant(ui.Transform)) < 1e-10f)
                return false;
            auto inv = glm::inverse(ui.Transform);
            auto o = glm::vec3(inv * glm::vec4(origin, 1)), d = glm::vec3(inv * glm::vec4(ray, 0));
            if (std::abs(d.z) < 1e-8f || (!ui.DoubleSided && d.z > 0))
                return false;
            distance = -o.z / d.z;
            if (distance < 0 || !std::isfinite(distance))
                return false;
            p = glm::vec2(o + d * distance);
            return true;
        };
        struct Candidate
        {
            const ViewUI* UI;
            glm::vec2     Point;
            float         Distance;
        };
        std::vector<Candidate> candidates;
        if (input.PointerInside)
            for (auto ui : order)
            {
                glm::vec2 p;
                float     d;
                if (point(*ui, p, d))
                    candidates.push_back({ui, p, d});
            }
        std::stable_sort(candidates.begin(), candidates.end(), [](auto& a, auto& b) {
            if (a.UI->Screen != b.UI->Screen)
                return a.UI->Screen;
            if (!a.UI->Screen && a.Distance != b.Distance)
                return a.Distance < b.Distance;
            if (a.UI->SortOrder != b.UI->SortOrder)
                return a.UI->SortOrder > b.UI->SortOrder;
            return a.UI->EntityID > b.UI->EntityID;
        });
        UITarget  hit;
        glm::vec2 hitPoint = input.Pointer;
        bool      blocked  = false;
        for (auto& c : candidates)
        {
            std::function<bool(const std::vector<UIRuntimeWidget>&)> find = [&](auto& nodes) {
                for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
                {
                    auto& w = *it;
                    if (!w.Visible || !w.Enabled)
                        continue;
                    if (find(w.Children))
                        return true;
                    if (std::abs(glm::determinant(w.Transform)) < 1e-10f || !Inside(c.Point, w.Clip) ||
                        !Inside(Local(w, c.Point), {0, 0, w.Size.x, w.Size.y}))
                        continue;
                    if (Selectable(w))
                    {
                        hit = Target(*c.UI, w);
                        return true;
                    }
                    if (UIProperty<bool>(w.Data, "BlockPointer", false))
                    {
                        blocked = true;
                        return true;
                    }
                }
                return false;
            };
            if (find(c.UI->Instance->Widgets()))
            {
                hitPoint = c.Point;
                break;
            }
        }
        auto capturedPoint = [&](const UITarget& target) {
            for (auto ui : order)
                if (ui->Instance == target.Instance.lock() && ui->Screen == target.Screen &&
                    ui->EntityID == target.Entity)
                {
                    glm::vec2 p;
                    float     distance;
                    if (point(*ui, p, distance))
                        return p;
                }
            return input.Pointer;
        };
        if (hit != m_Hover)
        {
            Emit(m_Hover, UIEventType::PointerExit, capturedPoint(m_Hover));
            m_Hover = hit;
            Emit(hit, UIEventType::PointerEnter, hitPoint);
        }
        if (input.ButtonDown[0])
        {
            m_Press = hit;
            m_Down = m_Last = input.Pointer;
            m_LastCanvas    = hitPoint;
            m_Dragging      = false;
            SetFocus(hit);
            Emit(hit, UIEventType::PointerDown, hitPoint);
            Slider(hit, hitPoint);
            PlaceCaret(hit, hitPoint, input.Shift);
            if (auto scroll = Resolve(Ancestor(hit, UIWidgetType::ScrollView)))
                scroll->ScrollVelocity = {};
        }
        if (m_Press)
        {
            auto p = capturedPoint(m_Press);
            if (!m_Dragging && glm::length(input.Pointer - m_Down) > DragThreshold)
            {
                m_Dragging = true;
                Emit(m_Press, UIEventType::DragBegin, p);
            }
            if (m_Dragging && input.Pointer != m_Last)
            {
                auto delta = p - m_LastCanvas;
                Emit(m_Press, UIEventType::Drag, p, delta);
                Slider(m_Press, p);
                PlaceCaret(m_Press, p, true);
                Scroll(Ancestor(m_Press, UIWidgetType::ScrollView), delta, true, seconds);
            }
            m_LastCanvas = p;
            if (input.ButtonUp[0])
            {
                Emit(m_Press, UIEventType::PointerUp, p);
                if (m_Dragging)
                    Emit(m_Press, UIEventType::DragEnd, p);
                else if (hit == m_Press)
                {
                    auto firstEvent = m_Events.size();
                    Activate(hit);
                    for (size_t event = firstEvent; event < m_Events.size(); ++event)
                        if (m_Events[event].Type == UIEventType::Clicked)
                            m_Events[event].Position = hitPoint;
                }
                m_Press    = {};
                m_Dragging = false;
            }
        }
        m_Last = input.Pointer;
        if (input.Scroll != glm::vec2(0) && !blocked)
        {
            auto scroll = Ancestor(hit, UIWidgetType::ScrollView);
            Emit(scroll, UIEventType::Scroll, hitPoint, input.Scroll);
            Scroll(scroll, input.Scroll, false, seconds);
        }
        std::vector<UITarget> selectables;
        // SortOrder changes draw order, but Tab follows source/tree order (source IDs are not an ordering key).
        for (auto ui : order)
        {
            std::function<void(const std::vector<UIRuntimeWidget>&)> visit = [&](auto& nodes) {
                std::vector<const UIRuntimeWidget*> tree;
                for (auto& w : nodes)
                    tree.push_back(&w);
                std::stable_sort(tree.begin(), tree.end(), [](auto a, auto b) { return a->TreeOrder < b->TreeOrder; });
                for (auto w : tree)
                {
                    if (Selectable(*w))
                        selectables.push_back(Target(*ui, *w));
                    visit(w->Children);
                }
            };
            visit(ui->Instance->Widgets());
        }
        auto key = [&](KeyCode k) { return input.KeyDown.count(k) != 0; };
        if (key(KeyCode::Tab) && !selectables.empty())
        {
            auto at    = std::find(selectables.begin(), selectables.end(), m_Focus);
            int  index = at == selectables.end() ? (input.Shift ? 0 : -1) : int(at - selectables.begin());
            index      = (index + (input.Shift ? -1 : 1) + int(selectables.size())) % int(selectables.size());
            SetFocus(selectables[index]);
        }
        auto screenCenter = [&](const UITarget& target) {
            auto      widget = Resolve(target);
            glm::vec2 center(widget->Bounds.x + widget->Bounds.z, widget->Bounds.y + widget->Bounds.w);
            center *= .5f;
            if (target.Screen)
                return center;
            for (auto surface : order)
                if (surface->Instance == target.Instance.lock() && surface->EntityID == target.Entity &&
                    !surface->Screen)
                {
                    auto projected = vp * surface->Transform * glm::vec4(center, 0, 1);
                    if (projected.w <= 0)
                        return glm::vec2(std::numeric_limits<float>::infinity());
                    return glm::vec2((projected.x / projected.w + 1) * pixels.x * .5f,
                                     (1 - projected.y / projected.w) * pixels.y * .5f);
                }
            return center;
        };
        auto w  = Resolve(m_Focus);
        bool up = input.Up || key(KeyCode::Up), down = input.Down || key(KeyCode::Down),
             left = input.Left || key(KeyCode::Left), right = input.Right || key(KeyCode::Right);
        if (w && w->Data.Type == UIWidgetType::InputField)
            Edit(m_Focus, input);
        else
        {
            if (w && w->Data.Type == UIWidgetType::Slider && (up || down || left || right))
            {
                auto  dir      = UIProperty<std::string>(w->Data, "Direction", "LeftToRight");
                bool  vertical = dir == "BottomToTop" || dir == "TopToBottom";
                bool  reverse  = dir == "RightToLeft" || dir == "BottomToTop";
                float sign     = vertical ? (down ? 1.f : up ? -1.f : 0.f) : (right ? 1.f : left ? -1.f : 0.f);
                if (reverse)
                    sign = -sign;
                float step = UIProperty<float>(
                    w->Data,
                    "Step",
                    UIProperty<bool>(w->Data, "WholeNumbers", false) ?
                        1.f :
                        (UIProperty<float>(w->Data, "Max", 1) - UIProperty<float>(w->Data, "Min", 0)) / 10);
                SetValue(m_Focus, YAML::Node(UIProperty<float>(w->Data, "Value", 0) + sign * step));
            }
            else if (up || down || left || right)
            {
                glm::vec2 direction = up   ? glm::vec2(0, -1) :
                                      down ? glm::vec2(0, 1) :
                                      left ? glm::vec2(-1, 0) :
                                             glm::vec2(1, 0);
                UITarget  next;
                if (w)
                {
                    auto name = UIProperty<std::string>(w->Data,
                                                        up   ? "NavUp" :
                                                        down ? "NavDown" :
                                                        left ? "NavLeft" :
                                                               "NavRight",
                                                        "");
                    for (auto& t : selectables)
                        if (t.Instance.lock() == m_Focus.Instance.lock() && t.Path == name)
                            next = t;
                    if (!next)
                    {
                        auto  center = screenCenter(m_Focus);
                        float best   = std::numeric_limits<float>::max();
                        for (auto& t : selectables)
                        {
                            if (t == m_Focus)
                                continue;
                            auto  d        = screenCenter(t) - center;
                            float distance = glm::dot(d, d);
                            if (glm::dot(d, direction) > .001f && distance < best)
                            {
                                best = distance;
                                next = t;
                            }
                        }
                    }
                }
                else if (!selectables.empty())
                    next = selectables.front();
                if (next)
                    SetFocus(next);
            }
            if (input.Submit || key(KeyCode::Enter) || key(KeyCode::KPEnter))
                Activate(m_Focus);
            if (input.Cancel || key(KeyCode::Escape))
                Emit(m_Focus, UIEventType::Cancelled);
        }
        for (auto ui : order)
        {
            auto same = [&](const UITarget& t) {
                return t.Instance.lock() == ui->Instance && t.Entity == ui->EntityID && t.Screen == ui->Screen ?
                           t.Path :
                           std::string();
            };
            std::vector<std::pair<UITarget, std::string>>            scrolling;
            std::function<void(const std::vector<UIRuntimeWidget>&)> remember = [&](auto& nodes) {
                for (auto& widget : nodes)
                {
                    if (widget.Data.Type == UIWidgetType::ScrollView)
                        scrolling.push_back({Target(*ui, widget), YAML::Dump(ui->Instance->Value(widget.Path))});
                    remember(widget.Children);
                }
            };
            remember(ui->Instance->Widgets());
            ui->Instance->Tick(seconds, same(m_Hover), same(m_Press), same(m_Focus));
            for (auto& [target, before] : scrolling)
                if (before != YAML::Dump(ui->Instance->Value(target.Path)))
                    Emit(target, UIEventType::Changed);
            ui->Instance->Update(
                ui->Screen ? pixels : ui->Instance->Data()->ReferenceResolution, m_Fonts, m_FontResolver);
        }
        // Callbacks run after traversal; they may mutate trees or unsubscribe safely.
        auto events    = m_Events;
        auto listeners = m_Listeners;
        for (auto& e : events)
            for (auto& [id, listener] : listeners)
                if (m_Listeners.count(id))
                    listener(e);
    }
} // namespace Kans
