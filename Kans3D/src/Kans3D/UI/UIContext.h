#pragma once
#include <functional>
#include <map>
#include "Kans3D/Core/UUID.h"
#include "UIInput.h"
#include "UIInstance.h"
namespace Kans
{
    struct ViewUI;
    struct UITarget
    {
        std::weak_ptr<UIInstance> Instance;
        std::string               Path, Key;
        UUID                      Entity = 0;
        bool                      Screen = true;
        explicit                  operator bool() const { return !Path.empty() && !Instance.expired(); }
        bool                      operator==(const UITarget& b) const
        {
            return Instance.lock() == b.Instance.lock() && Path == b.Path && Key == b.Key && Entity == b.Entity &&
                   Screen == b.Screen;
        }
        bool operator!=(const UITarget& b) const { return !(*this == b); }
    };
    enum class UIEventType
    {
        PointerEnter,
        PointerExit,
        PointerDown,
        PointerUp,
        Clicked,
        DragBegin,
        Drag,
        DragEnd,
        Scroll,
        Focused,
        Blurred,
        Changed,
        Submitted,
        Cancelled
    };
    struct UIEvent
    {
        UITarget    Target;
        UIEventType Type;
        glm::vec2   Position {0}, Delta {0}; // Canvas pixels; scroll delta is wheel units.
        int         Button = 0;
        YAML::Node  Value; // bool, float, vec2 or UTF-8 string, depending on widget type.
    };
    class UIContext
    {
    public:
        // projectionView uses the engine's OpenGL depth convention, before RHI conversion.
        void Process(const UIInputState&,
                     const std::vector<ViewUI>&,
                     glm::vec2        pixels,
                     const glm::mat4& projectionView = glm::mat4(1),
                     float            seconds        = 1.f / 60);
        void Process(UIInputState               input,
                     const std::vector<ViewUI>& uis,
                     const UIViewport&          viewport,
                     const glm::mat4&           projectionView = glm::mat4(1),
                     float                      seconds        = 1.f / 60)
        {
            input.PointerInside &= viewport.Contains(input.Pointer);
            input.Pointer = viewport.Map(input.Pointer);
            Process(input, uis, viewport.Pixels(), projectionView, seconds);
        }
        const std::vector<UIEvent>& Events() const { return m_Events; } // Replaced on Process; payloads are snapshots.
        const UITarget&             Focus() const { return m_Focus; }
        bool                        SetFocus(const UITarget&);
        bool                        SetValue(const UITarget&, const YAML::Node&);
        using Listener = std::function<void(const UIEvent&)>;
        uint64_t Subscribe(Listener listener)
        {
            auto id = ++m_NextListener;
            m_Listeners.emplace(id, std::move(listener));
            return id;
        }
        void Unsubscribe(uint64_t id) { m_Listeners.erase(id); }
        // GLFWwindow* is intentionally opaque here. Tests can inject clipboard callbacks without GLFW.
        void SetClipboardWindow(void* window) { m_ClipboardWindow = window; }
        void SetClipboard(std::function<std::string()> read, std::function<void(const std::string&)> write)
        {
            m_ReadClipboard  = std::move(read);
            m_WriteClipboard = std::move(write);
        }
        void SetFonts(FontSystem* fonts, UIInstance::FontResolver resolver = {})
        {
            m_Fonts        = fonts;
            m_FontResolver = std::move(resolver);
        }
        float DragThreshold = 5;
        void  Reset();

    private:
        UIRuntimeWidget*             Resolve(const UITarget&) const;
        void                         Emit(const UITarget&, UIEventType, glm::vec2 position = {}, glm::vec2 delta = {});
        void                         Activate(const UITarget&);
        void                         Slider(const UITarget&, glm::vec2 position);
        void                         Scroll(const UITarget&, glm::vec2 delta, bool dragging, float seconds);
        void                         Edit(const UITarget&, const UIInputState&);
        void                         PlaceCaret(const UITarget&, glm::vec2, bool extend);
        UITarget                     Ancestor(const UITarget&, UIWidgetType) const;
        std::vector<UIEvent>         m_Events;
        UITarget                     m_Focus, m_Hover, m_Press;
        glm::vec2                    m_Down {0}, m_Last {0}, m_LastCanvas {0};
        bool                         m_Dragging = false;
        std::map<uint64_t, Listener> m_Listeners;
        uint64_t                     m_NextListener = 0;
        FontSystem*                  m_Fonts        = nullptr;
        UIInstance::FontResolver     m_FontResolver;
        void*                        m_ClipboardWindow = nullptr;
        std::function<std::string()> m_ReadClipboard;
        std::function<void(const std::string&)> m_WriteClipboard;
    };
} // namespace Kans
