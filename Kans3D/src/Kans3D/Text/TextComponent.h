#pragma once
#include <optional>
#include "Kans3D/Core/Math/Bounds.h"
#include "TextLayout.h"

namespace Kans
{
    enum class TextBillboard
    {
        None,
        Full,
        Vertical
    };
    enum class TextPivotVertical
    {
        Top,
        Middle,
        Bottom,
        Baseline
    };
    struct TextComponent
    {
        std::string Text, Language;
        AssetID     Font;
        float       Size = .25f;
        glm::vec4   Color {1};
        struct AlignmentStyle
        {
            TextHorizontalAlignment Horizontal = TextHorizontalAlignment::Left;
            TextPivotVertical       Vertical   = TextPivotVertical::Top;
        } Alignment;
        float        WrapWidth = 0, BoxHeight = 0, LineSpacing = 1, LetterSpacing = 0;
        TextOverflow Overflow = TextOverflow::Wrap;
        struct OutlineStyle
        {
            float     Width = 0;
            glm::vec4 Color {0, 0, 0, 1};
        } Outline;
        struct ShadowStyle
        {
            glm::vec2 Offset {0};
            float     Softness = 0;
            glm::vec4 Color {0, 0, 0, 0};
        } Shadow;
        TextBillboard Billboard   = TextBillboard::None;
        bool          AlwaysOnTop = false, DoubleSided = true, CastShadows = false;
    };
    inline bool operator==(const TextComponent& a, const TextComponent& b)
    {
        return a.Text == b.Text && a.Language == b.Language && a.Font == b.Font && a.Size == b.Size &&
               a.Color == b.Color && a.Alignment.Horizontal == b.Alignment.Horizontal &&
               a.Alignment.Vertical == b.Alignment.Vertical && a.WrapWidth == b.WrapWidth &&
               a.BoxHeight == b.BoxHeight && a.LineSpacing == b.LineSpacing && a.LetterSpacing == b.LetterSpacing &&
               a.Overflow == b.Overflow && a.Outline.Width == b.Outline.Width && a.Outline.Color == b.Outline.Color &&
               a.Shadow.Offset == b.Shadow.Offset && a.Shadow.Softness == b.Shadow.Softness &&
               a.Shadow.Color == b.Shadow.Color && a.Billboard == b.Billboard && a.AlwaysOnTop == b.AlwaysOnTop &&
               a.DoubleSided == b.DoubleSided && a.CastShadows == b.CastShadows;
    }
    // Runtime cache belongs to a scene entity, never to its serialized component or snapshots.
    class TextLayoutCache
    {
    public:
        bool              Update(const TextComponent&, Ref<const FontAsset> font = {}, FontSystem* fonts = nullptr);
        const TextLayout& Get() const { return m_Layout; }
        uint64_t          Revision() const { return m_Revision; }
        Bounds            LocalBounds() const;

    private:
        std::optional<TextComponent> m_Style;
        Ref<const FontAsset>         m_Font;
        TextLayout                   m_Layout;
        uint64_t                     m_Revision = 0;
    };
    glm::mat4
    TextWorldTransform(const TextComponent&, const glm::mat4& world, glm::vec3 camera, const glm::mat4& cameraView);
    Bounds TextWorldBounds(const TextComponent&, const Bounds& local, const glm::mat4& world);
} // namespace Kans
