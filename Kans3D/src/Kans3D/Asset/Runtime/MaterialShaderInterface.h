#pragma once
#include <glm/glm.hpp>
#include "ShaderData.h"

namespace Kans
{
    // Contract between material Shaders and the renderer.
    //
    // Descriptor set 0 holds engine data, declared by `#include <Kans3D/Engine.glsl>`;
    // set 1 holds material data: uniform blocks filled from material parameters and
    // 2D samplers bound to material textures. Vertex inputs follow the engine mesh
    // layout (`#include <Kans3D/VertexInput.glsl>`); the fragment stage writes a
    // vec4 color to location 0.
    constexpr uint32_t EngineDescriptorSet   = 0;
    constexpr uint32_t MaterialDescriptorSet = 1;
    constexpr uint32_t EngineLightCount      = 4;

    enum class EngineBinding : uint32_t
    {
        Frame         = 0,
        Object        = 1,
        IrradianceMap = 2,
        PrefilterMap  = 3,
        BrdfLUT       = 4,
    };
    constexpr uint32_t EngineBindingCount = 5;

    // std140 images of the engine uniform blocks.
    struct EngineFrameUniforms
    {
        glm::mat4 ViewProjection {1.0f};
        glm::vec4 CameraPosition {0.0f};
        glm::vec4 LightPositions[EngineLightCount] {};
        // Radiance used by the engine PBR shading (the point light's ambient intensity).
        glm::vec4 LightColors[EngineLightCount] {};
        // The point light's diffuse intensity, for non-PBR (e.g. toon) shading.
        glm::vec4 LightDiffuse[EngineLightCount] {};
        float     Time = 0.0f;
        float     DebugView = 0.0f; // 0 lit, 1 base color, 2 world normals.
        float     Padding[2] {};
    };
    static_assert(sizeof(EngineFrameUniforms) == 288, "KansFrame std140 layout");
    static_assert(offsetof(EngineFrameUniforms, DebugView) == 276, "KansFrame debug uses existing tail padding");

    struct EngineObjectUniforms
    {
        glm::mat4 Transform {1.0f};
        glm::mat4 NormalMatrix {1.0f};
    };
    static_assert(sizeof(EngineObjectUniforms) == 128, "KansObject std140 layout");

    // GLSL text of an engine include (`#include <name>`), or nullptr.
    [[nodiscard]] const std::string* FindEngineShaderInclude(const std::string& name);

    // Checks a reflected program against the material contract.
    [[nodiscard]] std::variant<std::monostate, AssetError>
    ValidateMaterialShaderInterface(const std::vector<ShaderStageReflection>& reflection);

    // Material-set descriptors of a validated program, merged across stages and
    // ordered by binding.
    [[nodiscard]] std::vector<ShaderResourceBinding>
    MaterialShaderResources(const std::vector<ShaderStageReflection>& reflection);
} // namespace Kans
