#include "kspch.h"
#include "MaterialShaderInterface.h"
#include <map>

namespace Kans
{
    namespace
    {
        const std::map<std::string, std::string>& EngineIncludes()
        {
            static const std::map<std::string, std::string> includes = {
                {"Kans3D/Engine.glsl", R"(#ifndef KANS3D_ENGINE_GLSL
#define KANS3D_ENGINE_GLSL
#define KANS_LIGHT_COUNT 4
// Engine data (descriptor set 0). Matrices and positions are in world space.
layout(set = 0, binding = 0, std140) uniform KansFrame
{
    mat4  ViewProjection;
    vec4  CameraPosition;
    vec4  LightPositions[KANS_LIGHT_COUNT];
    vec4  LightColors[KANS_LIGHT_COUNT];  // PBR radiance
    vec4  LightDiffuse[KANS_LIGHT_COUNT]; // diffuse intensity for non-PBR shading
    float Time;
    float DebugView;
} u_Frame;
layout(set = 0, binding = 1, std140) uniform KansObject
{
    mat4 Transform;
    mat4 NormalMatrix;
    vec4 MaterialRender; // x: alpha mode (0 opaque / 1 mask / 2 blend), y: cutoff, z: double-sided
} u_Object;
layout(set = 0, binding = 2) uniform samplerCube u_IrradianceMap;
layout(set = 0, binding = 3) uniform samplerCube u_PrefilterMap;
layout(set = 0, binding = 4) uniform sampler2D   u_BrdfLUT;
#endif
)"},
                {"Kans3D/Alpha.glsl", R"(#ifndef KANS3D_ALPHA_GLSL
#define KANS3D_ALPHA_GLSL
#include <Kans3D/Engine.glsl>
// Fragment-stage helpers. Call with the final base colour alpha, before any debug-view return.
void KansAlphaClip(float alpha)
{
    if (u_Object.MaterialRender.x == 1.0 && alpha < u_Object.MaterialRender.y)
        discard;
}
float KansOutputAlpha(float alpha)
{
    return u_Object.MaterialRender.x == 2.0 ? alpha : 1.0;
}
#endif
)"},
                {"Kans3D/VertexInput.glsl", R"(#ifndef KANS3D_VERTEX_INPUT_GLSL
#define KANS3D_VERTEX_INPUT_GLSL
// Engine mesh vertex layout.
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_UV;
layout(location = 3) in vec4 a_Color;
layout(location = 4) in vec4 a_Tangent;
#endif
)"},
                {"Kans3D/Lighting.glsl", R"(#ifndef KANS3D_LIGHTING_GLSL
#define KANS3D_LIGHTING_GLSL
#include <Kans3D/Engine.glsl>
// Engine PBR lighting (point lights + image-based lighting), in linear HDR.
const float KANS_PI = 3.14159265359;

float KansDistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a     = roughness * roughness;
    float a2    = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (KANS_PI * denom * denom);
}

float KansGeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

vec3 KansShadePbr(vec3 albedo, float metallic, float roughness, float ao, vec3 N, vec3 worldPos)
{
    roughness = clamp(roughness, 0.04, 1.0);
    metallic  = clamp(metallic, 0.0, 1.0);
    vec3 V    = normalize(u_Frame.CameraPosition.xyz - worldPos);
    vec3 R    = reflect(-V, N);
    vec3 F0   = mix(vec3(0.04), albedo, metallic);
    float NdotV = max(dot(N, V), 0.0);

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < KANS_LIGHT_COUNT; ++i)
    {
        vec3  toLight  = u_Frame.LightPositions[i].xyz - worldPos;
        vec3  L        = normalize(toLight);
        vec3  H        = normalize(V + L);
        vec3  radiance = u_Frame.LightColors[i].rgb / max(dot(toLight, toLight), 1e-4);
        float NdotL    = max(dot(N, L), 0.0);
        float G        = KansGeometrySchlickGGX(NdotV, roughness) * KansGeometrySchlickGGX(NdotL, roughness);
        vec3  F        = F0 + (1.0 - F0) * pow(clamp(1.0 - max(dot(H, V), 0.0), 0.0, 1.0), 5.0);
        vec3  specular = KansDistributionGGX(N, H, roughness) * G * F / (4.0 * NdotV * NdotL + 0.001);
        vec3  kD       = (vec3(1.0) - F) * (1.0 - metallic);
        Lo += (kD * albedo / KANS_PI + specular) * radiance * NdotL;
    }

    vec3 F  = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse     = textureLod(u_IrradianceMap, N, 0.0).rgb * albedo;
    vec3 prefiltered = textureLod(u_PrefilterMap, R, roughness * 4.0).rgb;
    vec2 brdf        = texture(u_BrdfLUT, vec2(NdotV, roughness)).rg;
    return (kD * diffuse + prefiltered * (F * brdf.x + brdf.y)) * ao + Lo;
}

// Reinhard tone mapping and gamma encoding for the LDR scene target.
vec4 KansToneMap(vec3 color)
{
    color = color / (color + vec3(1.0));
    return vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
#endif
)"},
            };
            return includes;
        }

        struct ExpectedMember
        {
            const char* Name;
            uint64_t    Offset;
            uint32_t    Components, Columns, ArrayCount;
        };
        struct ExpectedBinding
        {
            ShaderBindingKind              Kind;
            ShaderImageDimension           Dimension;
            uint64_t                       BlockBytes;
            std::vector<ExpectedMember>    Members;
        };

        const ExpectedBinding& Expected(EngineBinding binding)
        {
            static const ExpectedBinding table[EngineBindingCount] = {
                {ShaderBindingKind::UniformBuffer,
                 ShaderImageDimension::Texture2D,
                 sizeof(EngineFrameUniforms),
                 {{"ViewProjection", 0, 4, 4, 1},
                  {"CameraPosition", 64, 4, 1, 1},
                  {"LightPositions", 80, 4, 1, EngineLightCount},
                  {"LightColors", 144, 4, 1, EngineLightCount},
                  {"LightDiffuse", 208, 4, 1, EngineLightCount},
                  {"Time", 272, 1, 1, 1},
                  {"DebugView", 276, 1, 1, 1}}},
                {ShaderBindingKind::UniformBuffer,
                 ShaderImageDimension::Texture2D,
                 sizeof(EngineObjectUniforms),
                 {{"Transform", 0, 4, 4, 1}, {"NormalMatrix", 64, 4, 4, 1}, {"MaterialRender", 128, 4, 1, 1}}},
                {ShaderBindingKind::CombinedImageSampler, ShaderImageDimension::Cube, 0, {}},
                {ShaderBindingKind::CombinedImageSampler, ShaderImageDimension::Cube, 0, {}},
                {ShaderBindingKind::CombinedImageSampler, ShaderImageDimension::Texture2D, 0, {}},
            };
            return table[static_cast<uint32_t>(binding)];
        }

        AssetError Contract(const std::string& message)
        {
            return AssetError {AssetErrorCode::InvalidData, "Material Shader contract: " + message};
        }

        // Published shaders before DebugView contain only the original KansFrame members.
        constexpr size_t LegacyFrameMemberCount = 6;
        constexpr size_t LegacyObjectMemberCount = 2;

        std::optional<AssetError> CheckEngineBinding(const ShaderResourceBinding& binding)
        {
            if (binding.Binding >= EngineBindingCount)
                return Contract("set 0 binding " + std::to_string(binding.Binding) +
                                " is not engine data; declare engine data with #include <Kans3D/Engine.glsl>");
            const auto& expected = Expected(static_cast<EngineBinding>(binding.Binding));
            // Block sizes are reflected without tail padding; members pin the layout.
            bool matches =
                binding.Kind == expected.Kind && binding.Count == 1 && binding.BlockBytes <= expected.BlockBytes &&
                (binding.Kind != ShaderBindingKind::CombinedImageSampler || binding.Dimension == expected.Dimension) &&
                (binding.Members.size() == expected.Members.size() ||
                 (binding.Binding == uint32_t(EngineBinding::Frame) &&
                  binding.Members.size() == LegacyFrameMemberCount) ||
                 (binding.Binding == uint32_t(EngineBinding::Object) &&
                  binding.Members.size() == LegacyObjectMemberCount));
            for (size_t i = 0; matches && i < binding.Members.size(); ++i)
            {
                const auto& member = binding.Members[i];
                const auto& want   = expected.Members[i];
                matches = member.Name == want.Name && member.Offset == want.Offset &&
                          member.Type == ShaderUniformType::Float && member.Components == want.Components &&
                          member.Columns == want.Columns && member.ArrayCount == want.ArrayCount;
            }
            if (!matches)
                return Contract("engine binding '" + binding.Name +
                                "' does not match the engine layout; use #include <Kans3D/Engine.glsl>");
            return std::nullopt;
        }
    } // namespace

    const std::string* FindEngineShaderInclude(const std::string& name)
    {
        const auto found = EngineIncludes().find(name);
        return found == EngineIncludes().end() ? nullptr : &found->second;
    }

    std::variant<std::monostate, AssetError>
    ValidateMaterialShaderInterface(const std::vector<ShaderStageReflection>& reflection)
    {
        const ShaderStageReflection* vertex   = nullptr;
        const ShaderStageReflection* fragment = nullptr;
        for (const auto& stage : reflection)
        {
            if (stage.Stage == ShaderProductStage::Vertex)
                vertex = &stage;
            else if (stage.Stage == ShaderProductStage::Fragment)
                fragment = &stage;
        }
        if (!vertex || !fragment || reflection.size() != 2)
            return Contract("a vertex and a fragment stage are required");

        // Engine mesh layout: position, normal, uv, color, tangent.
        constexpr uint32_t vertexComponents[] = {3, 3, 2, 4, 4};
        for (const auto& input : vertex->Inputs)
            if (input.Location >= 5 || input.Columns != 1 || input.Scalar != ShaderInterfaceScalar::Float ||
                input.Components != vertexComponents[input.Location])
                return Contract("vertex input '" + input.Name + "' does not match the engine vertex layout");
        if (fragment->Outputs.size() != 1 || fragment->Outputs[0].Location != 0 ||
            fragment->Outputs[0].Components != 4 || fragment->Outputs[0].Columns != 1 ||
            fragment->Outputs[0].Scalar != ShaderInterfaceScalar::Float)
            return Contract("the fragment stage must write one vec4 color at location 0");

        for (const auto& stage : reflection)
            for (const auto& binding : stage.Bindings)
            {
                if (binding.Set == EngineDescriptorSet)
                {
                    if (auto error = CheckEngineBinding(binding))
                        return *error;
                }
                else if (binding.Set == MaterialDescriptorSet)
                {
                    if (binding.Kind == ShaderBindingKind::CombinedImageSampler &&
                        binding.Dimension != ShaderImageDimension::Texture2D)
                        return Contract("material sampler '" + binding.Name + "' must be a sampler2D");
                }
                else
                    return Contract("'" + binding.Name + "' uses descriptor set " + std::to_string(binding.Set) +
                                    "; only set 0 (engine) and set 1 (material) are available");
            }
        return std::monostate {};
    }

    std::vector<ShaderResourceBinding> MaterialShaderResources(const std::vector<ShaderStageReflection>& reflection)
    {
        std::map<uint32_t, ShaderResourceBinding> merged;
        for (const auto& stage : reflection)
            for (const auto& binding : stage.Bindings)
                if (binding.Set == MaterialDescriptorSet)
                    merged.emplace(binding.Binding, binding);
        std::vector<ShaderResourceBinding> result;
        for (auto& entry : merged)
            result.push_back(std::move(entry.second));
        return result;
    }
} // namespace Kans
