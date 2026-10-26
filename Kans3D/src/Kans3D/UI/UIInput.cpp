#include "kspch.h"
#include "UIInput.h"
#include "Kans3D/Core/Events/KeyEvent.h"
#include "Kans3D/Core/Events/MouseEvent.h"
namespace Kans
{
    void UIInputState::Feed(Event& e)
    {
        switch (e.GetEventType())
        {
            case EventType::MouseMoved: {
                auto& v = static_cast<MouseMoveEvent&>(e);
                Pointer = {v.GetX(), v.GetY()};
                break;
            }
            case EventType::MouseScrolled: {
                auto& v = static_cast<MouseScrolledEvent&>(e);
                Scroll += glm::vec2(v.GetXOffset(), v.GetYOffset());
                break;
            }
            case EventType::MouseButtonPressed:
            case EventType::MouseButtonReleased: {
                auto b = size_t(static_cast<MouseButtonEvent&>(e).GetMouseButton());
                if (b < Buttons.size())
                {
                    bool down                         = e.GetEventType() == EventType::MouseButtonPressed;
                    Buttons[b]                        = down;
                    (down ? ButtonDown : ButtonUp)[b] = true;
                }
                break;
            }
            case EventType::KeyTyped: {
                auto c = static_cast<KeyTypedEvent&>(e).GetCodepoint();
                if (c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff))
                    Typed += c;
                break;
            }
            case EventType::KeyPressed: {
                auto k = static_cast<KeyPressedEvent&>(e).GetKeyCode();
                KeyDown.insert(k);
                Keys.insert(k);
                break;
            }
            case EventType::KeyReleased: {
                auto k = static_cast<KeyReleasedEvent&>(e).GetKeyCode();
                KeyUp.insert(k);
                Keys.erase(k);
                break;
            }
            default:
                break;
        }
        Shift   = Keys.count(KeyCode::LeftShift) || Keys.count(KeyCode::RightShift);
        Control = Keys.count(KeyCode::LeftControl) || Keys.count(KeyCode::RightControl);
        Alt     = Keys.count(KeyCode::LeftAlt) || Keys.count(KeyCode::RightAlt);
        Super   = Keys.count(KeyCode::LeftSuper) || Keys.count(KeyCode::RightSuper);
    }
    void UIInputState::NextFrame()
    {
        ButtonDown.fill(false);
        ButtonUp.fill(false);
        KeyDown.clear();
        KeyUp.clear();
        Typed.clear();
        Scroll = {};
        Up = Down = Left = Right = Submit = Cancel = false;
    }
} // namespace Kans
