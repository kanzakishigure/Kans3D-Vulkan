#include "kspch.h"
#include "Renderer2D.h"
namespace Kans
{
    using namespace RenderHardware;
    namespace
    {
        std::unique_ptr<SpriteBatch> LegacyBatch;
        SpriteBatch&                 Batch()
        {
            if (!LegacyBatch)
                LegacyBatch = std::make_unique<SpriteBatch>();
            return *LegacyBatch;
        }
    } // namespace
    void Renderer2D::Init(std::shared_ptr<Device> device) { Batch().Init(device); }
    void Renderer2D::SetTarget(std::shared_ptr<Device> device, const RenderingDesc& target)
    {
        Batch().SetTarget(device, target);
    }
    void Renderer2D::SetSubmissionSink(SubmissionSink sink) { Batch().SetSubmissionSink(sink); }
    void Renderer2D::SetCommandList(CommandList* commands) { Batch().SetCommandList(commands); }
    void Renderer2D::ClearTarget() { Batch().ClearTarget(); }
    void Renderer2D::BeginScene(const OrthographicCamera& camera, ImmediatePassState state)
    {
        Batch().BeginScene(camera, state);
    }
    void Renderer2D::BeginScene(const Camera& camera, const glm::mat4& view, ImmediatePassState state)
    {
        Batch().BeginScene(camera, view, state);
    }
    void Renderer2D::EndScene() { Batch().EndScene(); }
    void Renderer2D::Shutdown()
    {
        if (LegacyBatch)
            LegacyBatch->Shutdown();
        LegacyBatch.reset();
    }
    void Renderer2D::Flush() { Batch().Flush(); }
    void Renderer2D::DrawQuad(const glm::vec2& p, const glm::vec2& s, const glm::vec4& c) { Batch().DrawQuad(p, s, c); }
    void Renderer2D::DrawQuad(const glm::vec3& p, const glm::vec2& s, const glm::vec4& c) { Batch().DrawQuad(p, s, c); }
    void Renderer2D::DrawQuad(const glm::mat4& t, const glm::vec4& c) { Batch().DrawQuad(t, c); }
    void Renderer2D::DrawQuad(const glm::vec2&      p,
                              const glm::vec2&      s,
                              const Ref<Texture2D>& image,
                              const glm::vec4&      c,
                              float                 tiling)
    {
        Batch().DrawQuad(p, s, image, c, tiling);
    }
    void Renderer2D::DrawQuad(const glm::vec3&      p,
                              const glm::vec2&      s,
                              const Ref<Texture2D>& image,
                              const glm::vec4&      c,
                              float                 tiling)
    {
        Batch().DrawQuad(p, s, image, c, tiling);
    }
    void Renderer2D::DrawQuad(const glm::mat4& t, const Ref<Texture2D>& image, const glm::vec4& c, float tiling)
    {
        Batch().DrawQuad(t, image, c, tiling);
    }
    void Renderer2D::DrawQuad(const glm::vec2&         p,
                              const glm::vec2&         s,
                              const Ref<SubTexture2D>& image,
                              const glm::vec4&         c,
                              float                    tiling)
    {
        Batch().DrawQuad(p, s, image, c, tiling);
    }
    void Renderer2D::DrawQuad(const glm::vec3&         p,
                              const glm::vec2&         s,
                              const Ref<SubTexture2D>& image,
                              const glm::vec4&         c,
                              float                    tiling)
    {
        Batch().DrawQuad(p, s, image, c, tiling);
    }
    void Renderer2D::DrawQuad(const glm::mat4& t, const Ref<SubTexture2D>& image, const glm::vec4& c, float tiling)
    {
        Batch().DrawQuad(t, image, c, tiling);
    }
    void Renderer2D::DrawRotateQuad(const glm::vec2& p, float r, const glm::vec2& s, const glm::vec4& c)
    {
        Batch().DrawRotateQuad(p, r, s, c);
    }
    void Renderer2D::DrawRotateQuad(const glm::vec3& p, float r, const glm::vec2& s, const glm::vec4& c)
    {
        Batch().DrawRotateQuad(p, r, s, c);
    }
    void Renderer2D::DrawRotateQuad(const glm::vec2&      p,
                                    float                 r,
                                    const glm::vec2&      s,
                                    const Ref<Texture2D>& image,
                                    const glm::vec4&      c,
                                    float                 tiling)
    {
        Batch().DrawRotateQuad(p, r, s, image, c, tiling);
    }
    void Renderer2D::DrawRotateQuad(const glm::vec3&      p,
                                    float                 r,
                                    const glm::vec2&      s,
                                    const Ref<Texture2D>& image,
                                    const glm::vec4&      c,
                                    float                 tiling)
    {
        Batch().DrawRotateQuad(p, r, s, image, c, tiling);
    }
    void                    Renderer2D::ResetStats() { Batch().ResetStats(); }
    Renderer2D::Statistics& Renderer2D::GetStats() { return Batch().GetStats(); }
} // namespace Kans
