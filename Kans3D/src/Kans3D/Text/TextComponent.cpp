#include "kspch.h"
#include "TextComponent.h"
#include <glm/gtc/matrix_transform.hpp>
namespace Kans
{
    bool TextLayoutCache::Update(const TextComponent& c, Ref<const FontAsset> font, FontSystem* fonts)
    {
        if (m_Style && *m_Style == c && m_Font == font && m_Layout.IsAtlasCurrent())
        {
            m_Layout.TouchGlyphs();
            return false;
        }
        TextLayoutRequest r;
        r.Text          = c.Text;
        r.Language      = c.Language;
        r.Font          = font;
        r.Fonts         = fonts;
        r.Size          = c.Size;
        r.WrapWidth     = c.WrapWidth;
        r.BoxHeight     = c.BoxHeight;
        r.Horizontal    = c.Alignment.Horizontal;
        r.LineSpacing   = c.LineSpacing;
        r.LetterSpacing = c.LetterSpacing;
        r.Overflow      = c.Overflow;
        r.MinimumSize   = std::max(.001f, c.Size / 16);
        m_Layout        = Layout(r);
        auto  b         = m_Layout.Bounds;
        float x         = c.Alignment.Horizontal == TextHorizontalAlignment::Center ? (b.x + b.z) * .5f :
                          c.Alignment.Horizontal == TextHorizontalAlignment::Right  ? b.z :
                                                                                      b.x;
        float y         = c.Alignment.Vertical == TextPivotVertical::Middle ? (b.y + b.w) * .5f :
                          c.Alignment.Vertical == TextPivotVertical::Bottom ? b.w :
                          c.Alignment.Vertical == TextPivotVertical::Baseline && !m_Layout.Lines.empty() ?
                                                                              m_Layout.Lines.front().BaselineY :
                                                                              b.y;
        // UI1 is Y down. Scene text uses local +Y up, with its front along +Z.
        for (auto& q : m_Layout.Glyphs)
            q.Position = {q.Position.x - x, y - q.Position.y, q.Position.z - x, y - q.Position.w};
        for (auto& l : m_Layout.Lines)
            l.BaselineY = y - l.BaselineY;
        for (auto& p : m_Layout.Carets)
            p.Position = {p.Position.x - x, y - p.Position.y};
        m_Layout.Bounds = {b.x - x, y - b.w, b.z - x, y - b.y};
        m_Style         = c;
        m_Font          = std::move(font);
        ++m_Revision;
        return true;
    }
    Bounds TextLayoutCache::LocalBounds() const
    {
        Bounds b;
        if (!m_Style || m_Style->Text.empty())
            return b;
        const auto r = m_Layout.Bounds;
        b.Expand({r.x, r.y, 0});
        b.Expand({r.z, r.w, 0});
        for (auto& q : m_Layout.Glyphs)
        {
            b.Expand({q.Position.x, q.Position.y, 0});
            b.Expand({q.Position.z, q.Position.w, 0});
        }
        const auto& c      = *m_Style;
        const float e      = std::max(c.Outline.Width, c.Shadow.Softness) * m_Layout.SizeUsed;
        const auto  offset = c.Shadow.Offset * m_Layout.SizeUsed;
        auto        min = b.Min, max = b.Max;
        b.Expand(min + glm::vec3(glm::min(offset, glm::vec2(0)) - e, -.001f));
        b.Expand(max + glm::vec3(glm::max(offset, glm::vec2(0)) + e, .001f));
        return b;
    }
    glm::mat4
    TextWorldTransform(const TextComponent& c, const glm::mat4& world, glm::vec3 camera, const glm::mat4& cameraView)
    {
        if (c.Billboard == TextBillboard::None)
            return world;
        glm::vec3 scale(
            glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])));
        auto      inverseView = glm::inverse(cameraView);
        glm::vec3 up = c.Billboard == TextBillboard::Vertical ? glm::vec3(world[1]) : glm::vec3(inverseView[1]);
        if (glm::length(up) < 1e-6f)
            up = {0, 1, 0};
        up              = glm::normalize(up);
        glm::vec3 front = c.Billboard == TextBillboard::Full ? glm::vec3(inverseView[2]) : camera - glm::vec3(world[3]);
        front -= up * glm::dot(front, up);
        if (glm::length(front) < 1e-6f)
            front = glm::cross(up, glm::abs(up.x) < .9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1));
        front      = glm::normalize(front);
        auto right = glm::normalize(glm::cross(up, front));
        return {glm::vec4(right * scale.x, 0), glm::vec4(up * scale.y, 0), glm::vec4(front * scale.z, 0), world[3]};
    }
    Bounds TextWorldBounds(const TextComponent& c, const Bounds& local, const glm::mat4& world)
    {
        if (!local.IsValid() || c.Billboard == TextBillboard::None)
            return local.Transformed(world);
        const auto extent = glm::max(glm::abs(local.Min), glm::abs(local.Max));
        float      radius = glm::length(glm::vec3(extent.x * glm::length(glm::vec3(world[0])),
                                                  extent.y * glm::length(glm::vec3(world[1])),
                                                  extent.z * glm::length(glm::vec3(world[2]))));
        Bounds     b;
        b.Expand(glm::vec3(world[3]) - radius);
        b.Expand(glm::vec3(world[3]) + radius);
        return b;
    }
} // namespace Kans
