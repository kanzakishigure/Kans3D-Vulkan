#pragma once
#include <cstring>
#include <shaderc/shaderc.hpp>
#include "Kans3D/Asset/Runtime/MaterialShaderInterface.h"

namespace Kans
{
    // Material shaders used by tests: an unlit vertex stage and a fragment stage
    // that multiplies a tint by a texture.
    inline const char* const TestMaterialVertex = R"(#version 450
#include <Kans3D/Engine.glsl>
#include <Kans3D/VertexInput.glsl>
layout(location = 0) out vec2 v_UV;
void main()
{
    v_UV        = a_UV;
    gl_Position = u_Frame.ViewProjection * u_Object.Transform * vec4(a_Position, 1.0);
}
)";
    inline const char* const TestMaterialFragment = R"(#version 450
layout(set = 1, binding = 0, std140) uniform Parameters
{
    vec4  Tint;
    float Strength;
    int   Steps;
    vec2  Offsets[2];
} p;
layout(set = 1, binding = 1) uniform sampler2D u_Albedo;
layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;
void main()
{
    o_Color = p.Tint * p.Strength * texture(u_Albedo, v_UV + p.Offsets[1]) + vec4(float(p.Steps) * 0.0);
}
)";

    // Resolves engine includes like the ShaderImporter does.
    class EngineIncluder final : public shaderc::CompileOptions::IncluderInterface
    {
    public:
        shaderc_include_result* GetInclude(const char* requested, shaderc_include_type, const char*, size_t) override
        {
            auto* result = new shaderc_include_result {};
            if (const auto* text = FindEngineShaderInclude(requested))
            {
                result->source_name        = requested;
                result->source_name_length = std::strlen(requested);
                result->content            = text->data();
                result->content_length     = text->size();
            }
            else
            {
                result->content        = "unknown include";
                result->content_length = std::strlen(result->content);
            }
            return result;
        }
        void ReleaseInclude(shaderc_include_result* result) override { delete result; }
    };

    inline std::vector<uint32_t> CompileTestStage(const std::string& source, shaderc_shader_kind kind)
    {
        shaderc::Compiler       compiler;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
        options.SetIncluder(std::make_unique<EngineIncluder>());
        auto compiled = compiler.CompileGlslToSpv(source, kind, "test", options);
        if (compiled.GetCompilationStatus() != shaderc_compilation_status_success)
            throw std::runtime_error(compiled.GetErrorMessage());
        return {compiled.cbegin(), compiled.cend()};
    }

    inline ShaderProgramCode CompileTestMaterialShader(const std::string& vertex   = TestMaterialVertex,
                                                       const std::string& fragment = TestMaterialFragment)
    {
        return {{{ShaderProductStage::Vertex, "main", CompileTestStage(vertex, shaderc_vertex_shader)},
                 {ShaderProductStage::Fragment, "main", CompileTestStage(fragment, shaderc_fragment_shader)}}};
    }
} // namespace Kans
