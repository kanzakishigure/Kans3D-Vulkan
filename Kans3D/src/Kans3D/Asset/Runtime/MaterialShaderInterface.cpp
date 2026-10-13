#include "kspch.h"
#include "MaterialShaderInterface.h"
#include <map>
#include <set>

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
#define KANS_POINT_LIGHT_LIMIT 16
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
    vec2 FramePadding;
    vec4 PointPositions[KANS_POINT_LIGHT_LIMIT];
    vec4 PointRadiance[KANS_POINT_LIGHT_LIMIT];
    vec4 PointSpecular[KANS_POINT_LIGHT_LIMIT];
    vec4 DirectionalDirection;
    vec4 DirectionalRadiance;
    vec4 DirectionalSpecular;
    vec4 DirectionalAmbient;
    vec4 LightingInfo;
} u_Frame;
int KansPointLightCount() { return clamp(int(u_Frame.LightingInfo.x), 0, KANS_POINT_LIGHT_LIMIT); }
float KansExposure() { return u_Frame.LightingInfo.y; }
vec3 KansPointLightPosition(int i) { return u_Frame.PointPositions[i].xyz; }
vec3 KansPointLightRadiance(int i) { return u_Frame.PointRadiance[i].rgb; }
vec3 KansPointLightSpecular(int i) { return u_Frame.PointSpecular[i].rgb; }
vec3 KansDirectionalToLight() { return -u_Frame.DirectionalDirection.xyz; }
vec3 KansDirectionalRadiance() { return u_Frame.DirectionalRadiance.rgb; }
vec3 KansDirectionalAmbient() { return u_Frame.DirectionalAmbient.rgb; }
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

vec3 KansDirectPbr(vec3 albedo, float metallic, float roughness, vec3 N, vec3 V,
                   vec3 L, vec3 radiance, vec3 specularScale)
{
    if (dot(N, L) <= 0.0) return vec3(0);
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NdotV = max(dot(N, V), 0.0), NdotL = max(dot(N, L), 0.0);
    float G = KansGeometrySchlickGGX(NdotV, roughness) * KansGeometrySchlickGGX(NdotL, roughness);
    vec3 F = F0 + (1.0 - F0) * pow(clamp(1.0 - max(dot(H, V), 0.0), 0.0, 1.0), 5.0);
    vec3 specular = KansDistributionGGX(N, H, roughness) * G * F / (4.0 * NdotV * NdotL + 0.001);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    return (kD * albedo / KANS_PI + specular * specularScale) * radiance * NdotL;
}
vec3 KansShadePbr(vec3 albedo, float metallic, float roughness, float ao, vec3 N, vec3 worldPos)
{
    roughness = clamp(roughness, 0.04, 1.0);
    metallic = clamp(metallic, 0.0, 1.0);
    vec3 V = normalize(u_Frame.CameraPosition.xyz - worldPos);
    vec3 R = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NdotV = max(dot(N, V), 0.0);
    vec3 Lo = vec3(0.0);
    for (int i = 0; i < KansPointLightCount(); ++i)
    {
        if (all(lessThanEqual(KansPointLightRadiance(i), vec3(0)))) continue;
        vec3 toLight = KansPointLightPosition(i) - worldPos;
        if (dot(toLight, toLight) <= 1e-8) continue;
        float distanceSquared = max(dot(toLight, toLight), 1e-4);
        vec3 L = normalize(toLight);
        Lo += KansDirectPbr(albedo, metallic, roughness, N, V, L,
                            KansPointLightRadiance(i) / distanceSquared, KansPointLightSpecular(i));
    }
    if (u_Frame.DirectionalDirection.w != 0.0)
        Lo += KansDirectPbr(albedo, metallic, roughness, N, V, KansDirectionalToLight(),
                            KansDirectionalRadiance(), u_Frame.DirectionalSpecular.rgb);
    vec3 F = F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = textureLod(u_IrradianceMap, N, 0.0).rgb * albedo;
    vec3 prefiltered = textureLod(u_PrefilterMap, R, roughness * 4.0).rgb;
    vec2 brdf = texture(u_BrdfLUT, vec2(NdotV, roughness)).rg;
    return (kD * diffuse + prefiltered * (F * brdf.x + brdf.y)
            + KansDirectionalAmbient() * albedo) * ao + Lo;
}

// Legacy display-referred helper. Retained for published/custom sources; HDR shaders return linear colour.
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
                  {"DebugView", 276, 1, 1, 1},
                  {"FramePadding", 280, 2, 1, 1},
                  {"PointPositions", 288, 4, 1, EnginePointLightLimit},
                  {"PointRadiance", 544, 4, 1, EnginePointLightLimit},
                  {"PointSpecular", 800, 4, 1, EnginePointLightLimit},
                  {"DirectionalDirection", 1056, 4, 1, 1},
                  {"DirectionalRadiance", 1072, 4, 1, 1},
                  {"DirectionalSpecular", 1088, 4, 1, 1},
                  {"DirectionalAmbient", 1104, 4, 1, 1},
                  {"LightingInfo", 1120, 4, 1, 1}}},
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
                  (binding.Members.size() == LegacyFrameMemberCount || binding.Members.size() == 7)) ||
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

    namespace
    {
        bool MemberSubset(const ShaderUniformMember& a, const ShaderUniformMember& b)
        {
            if (a.Name != b.Name || a.Offset != b.Offset || a.Type != b.Type || a.Components != b.Components ||
                a.Columns != b.Columns || a.ArrayCount != b.ArrayCount || a.ArrayStride != b.ArrayStride ||
                a.MatrixStride != b.MatrixStride || a.RowMajor != b.RowMajor || a.Bytes > b.Bytes)
                return false;
            if (a.Type != ShaderUniformType::Struct && a.Bytes != b.Bytes)
                return false;
            for (const auto& member : a.Members)
            {
                auto found = std::find_if(
                    b.Members.begin(), b.Members.end(), [&](const auto& want) { return want.Name == member.Name; });
                if (found == b.Members.end() || !MemberSubset(member, *found))
                    return false;
            }
            return true;
        }
        bool BindingSubset(const ShaderResourceBinding& a, const ShaderResourceBinding& b)
        {
            if (a.Kind != b.Kind || a.Set != b.Set || a.Binding != b.Binding || a.Count != b.Count ||
                a.Dimension != b.Dimension || a.Name != b.Name || a.BlockBytes > b.BlockBytes)
                return false;
            for (const auto& member : a.Members)
            {
                auto found = std::find_if(
                    b.Members.begin(), b.Members.end(), [&](const auto& want) { return want.Name == member.Name; });
                if (found == b.Members.end() || !MemberSubset(member, *found))
                    return false;
            }
            return true;
        }
    } // namespace
    std::variant<std::monostate, AssetError> ValidateMaterialShaderPasses(const ShaderProgramCode&       main,
                                                                          const std::vector<ShaderPass>& passes)
    {
        if (passes.empty())
            return std::monostate {}; // Generic single-stage products retain their existing contract.
        auto inspected = InspectShaderProgram(main);
        if (auto* error = std::get_if<AssetError>(&inspected))
            return *error;
        const auto& reflection = std::get<std::vector<ShaderStageReflection>>(inspected);
        auto        valid      = ValidateMaterialShaderInterface(reflection);
        if (auto* error = std::get_if<AssetError>(&valid))
            return *error;
        std::map<std::pair<uint32_t, uint32_t>, ShaderResourceBinding> bindings;
        for (const auto& stage : reflection)
            for (const auto& binding : stage.Bindings)
            {
                auto [found, inserted] = bindings.emplace(std::make_pair(binding.Set, binding.Binding), binding);
                if (!inserted && (!BindingSubset(binding, found->second) || !BindingSubset(found->second, binding)))
                    return Contract("main pass has inconsistent resource names/layouts across stages");
            }
        std::set<std::string> names;
        if (passes.size() > 32)
            return Contract("too many additional passes");
        for (const auto& pass : passes)
        {
            const auto fail = [&](const std::string& message) {
                return Contract("pass '" + pass.Name + "': " + message);
            };
            const auto token = [](const std::string& value) {
                return !value.empty() && value.size() <= 256 &&
                       std::all_of(
                           value.begin(), value.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
            };
            const auto& state = pass.State;
            if (!token(pass.Name) || pass.Name == "Main" || !token(pass.Tag) || !names.insert(pass.Name).second)
                return fail("invalid/duplicate name or tag");
            if (uint32_t(state.Cull) > 2 || uint32_t(state.DepthCompare) > 7 || state.ColorWriteMask > 15 ||
                uint32_t(state.SourceColor) > 9 || uint32_t(state.DestinationColor) > 9 ||
                uint32_t(state.SourceAlpha) > 9 || uint32_t(state.DestinationAlpha) > 9)
                return fail("invalid render state");
            auto extra = InspectShaderProgram(pass.Code);
            if (auto* error = std::get_if<AssetError>(&extra))
                return fail(error->Message);
            const auto& stages   = std::get<std::vector<ShaderStageReflection>>(extra);
            auto        contract = ValidateMaterialShaderInterface(stages);
            if (auto* error = std::get_if<AssetError>(&contract))
                return fail(error->Message);
            std::set<uint32_t> engine;
            for (const auto& stage : stages)
                for (const auto& binding : stage.Bindings)
                {
                    auto found = bindings.find({binding.Set, binding.Binding});
                    if (found == bindings.end() || !BindingSubset(binding, found->second))
                        return fail("set " + std::to_string(binding.Set) + " binding " +
                                    std::to_string(binding.Binding) + " ('" + binding.Name +
                                    "') is not a subset of the main pass layout");
                    if (binding.Set == 0)
                    {
                        if (!BindingSubset(found->second, binding))
                            return fail("set 0 contract differs from main pass");
                        engine.insert(binding.Binding);
                    }
                }
            for (const auto& [key, binding] : bindings)
                if (key.first == 0 && !engine.count(key.second))
                    return fail("set 0 contract omits main pass binding " + std::to_string(key.second));
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
    std::variant<std::monostate, AssetError>
    ValidateMaterialParameterGroups(const std::vector<MaterialParameterGroup>& groups,
                                    const std::vector<ShaderStageReflection>&  reflection)
    {
        std::set<std::string> names, used, groupNames;
        for (const auto& resource : MaterialShaderResources(reflection))
            if (resource.Kind == ShaderBindingKind::CombinedImageSampler)
                names.insert(resource.Name);
            else
                for (const auto& member : resource.Members)
                    if (member.Type != ShaderUniformType::Struct)
                        names.insert(member.Name);
        if (groups.size() > 4096)
            return AssetError {AssetErrorCode::InvalidData, "Too many Shader groups"};
        for (const auto& group : groups)
        {
            if (group.Name.empty() || group.Name.size() > 256 || group.Members.empty() || group.Members.size() > 4096 ||
                !groupNames.insert(group.Name).second || group.Name.front() == ' ' || group.Name.back() == ' ' ||
                std::any_of(group.Name.begin(), group.Name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                return AssetError {AssetErrorCode::InvalidData, "Invalid or duplicate Shader group name"};
            for (const auto& member : group.Members)
                if (!names.count(member) || !used.insert(member).second)
                    return AssetError {AssetErrorCode::InvalidData,
                                       "Unknown or repeated Shader group member '" + member + "'"};
        }
        return std::monostate {};
    }
} // namespace Kans
