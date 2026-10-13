#pragma once
#include "Kans3D/Renderer/Resource/RenderAssetCache.h"
#include "Kans3D/Renderer/Resource/SelectionTools.h"
#include "Resource/ToneMappingPass.h"

namespace Kans
{
    class SceneRenderer;
    enum class MaterialPreviewShape
    {
        Sphere,
        Cube,
        Plane
    };
    struct MaterialPreviewOptions
    {
        glm::uvec2           Size {256, 256};
        MaterialPreviewShape Shape = MaterialPreviewShape::Sphere;
        // Object rotation in radians: yaw about +Y, then pitch about +X.
        glm::vec2 Rotation {0.0f};
        float     Exposure = 0.8f;
    };
    struct MaterialPreviewResult
    {
        uintptr_t Texture  = 0;
        bool     Complete = false;
    };
    struct MaterialPreviewImageResult
    {
        RenderHardware::Handle<RenderHardware::Texture> Target;
        bool                                            Complete = false;
    };
    struct MaterialThumbnail
    {
        glm::uvec2 Size {};
        // RGBA8, tightly packed, top row first; alpha is always 255.
        std::vector<uint8_t> Pixels;
    };

    // Render thread only; the scene's device and its context must remain alive.
    // SceneRenderer must outlive this instance. Does not begin/end a scene/cache frame.
    // Inputs are borrowed only during the call. GPU cache resources are resolved at draw time.
    class MaterialPreviewRenderer
    {
    public:
        explicit MaterialPreviewRenderer(SceneRenderer& scene);
        ~MaterialPreviewRenderer();
        MaterialPreviewRenderer(const MaterialPreviewRenderer&)            = delete;
        MaterialPreviewRenderer& operator=(const MaterialPreviewRenderer&) = delete;

        // nullptr selects Standard. Requires scene.SetAssetManager, even for resolved inputs.
        // Texture belongs to this instance, valid until its next Render/RenderThumbnail or destruction.
        // Complete means every assigned asset and GPU upload was ready.
        // Unassigned samplers use declared fallbacks and are complete; unavailable assigned assets are not.
        MaterialPreviewResult
        Render(const MaterialData& material, const ShaderData* shader, const MaterialPreviewOptions& options = {});
        MaterialPreviewResult Render(AssetID material, const MaterialPreviewOptions& options = {});
        const RenderHardware::Handle<RenderHardware::Texture>& GetHDRTarget() const { return m_HDRColor; }
        // Offscreen rendering exposes a neutral image, without exporting it to the UI.
        MaterialPreviewImageResult
        RenderImage(const MaterialData& material, const ShaderData* shader, const MaterialPreviewOptions& options = {});
        MaterialPreviewImageResult RenderImage(AssetID material, const MaterialPreviewOptions& options = {});
        // No image while material/shader/assigned textures are missing, loading or failed.
        std::optional<MaterialThumbnail> RenderThumbnail(AssetID material, const MaterialPreviewOptions& options = {});

    private:
        MaterialPreviewImageResult                       Draw(const GpuShader&              shader,
                                                              const GpuMaterialBindings&    bindings,
                                                              const MaterialPreviewOptions& options,
                                                              bool                          complete);
        bool                                  Resize(glm::uvec2 size);
        SceneRenderer&                        m_Scene;
        glm::uvec2                            m_Size {};
        std::shared_ptr<RenderHardware::Device>          m_Device;
        RenderHardware::Handle<RenderHardware::Texture>  m_Color, m_Depth, m_HDRColor;
        ToneMappingPass                                  m_ToneMapping;
        std::array<std::optional<GpuMesh>, 3> m_Shapes;
        RenderHardware::Handle<RenderHardware::Pipeline> m_Checker;
        std::optional<ShaderData>             m_ShaderSource;
        Scope<GpuShader>                      m_Shader;
    };
} // namespace Kans
