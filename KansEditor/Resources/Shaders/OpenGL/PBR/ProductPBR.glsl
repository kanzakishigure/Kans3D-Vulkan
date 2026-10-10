#type vertex

#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_UV;
layout(location = 3) in vec4 a_Color;
layout(location = 4) in vec4 a_Tangent;

out vec2 TexCoords;
out vec3 WorldPos;
out vec3 Normal;
out vec4 VertexColor;

uniform mat4 U_ViewProjection;
uniform mat4 U_Transform;

void main()
{
    TexCoords   = a_UV;
    WorldPos    = vec3(U_Transform * vec4(a_Position, 1.0));
    Normal      = transpose(inverse(mat3(U_Transform))) * a_Normal;
    VertexColor = a_Color;
    gl_Position = U_ViewProjection * vec4(WorldPos, 1.0);
}

#type fragment
#version 330 core

out vec4 FragColor;
in vec2 TexCoords;
in vec3 WorldPos;
in vec3 Normal;
in vec4 VertexColor;

const float PI = 3.14159265359;

// IBL
uniform samplerCube U_IrradianceMap;
uniform samplerCube U_PrefilterMap;
uniform sampler2D   U_BrdfLUTMap;
uniform vec3        U_ViewPos;
uniform vec3        lightPositions[4];
uniform vec3        lightColors[4];

// Material factors multiply the optional textures. Textures are sampled from
// sRGB or linear formats as declared by the Texture Product, so values are linear.
uniform vec4      U_BaseColor;
uniform float     U_Metallic;
uniform float     U_Roughness;
uniform sampler2D U_AlbedoTexture;
uniform sampler2D U_NormalTexture;
uniform sampler2D U_MetallicRoughnessTexture;
uniform sampler2D U_OcclusionTexture;
uniform sampler2D U_EmissiveTexture;
uniform int       U_HasNormalTexture;
uniform int       U_HasEmissiveTexture;
// Editor view: 0 lit, 1 base color, 2 world normals.
uniform int       U_DebugView;

vec3 GetNormal()
{
    vec3 N = normalize(Normal);
    if (U_HasNormalTexture == 0)
        return N;
    vec3 tangentNormal = texture(U_NormalTexture, TexCoords).xyz * 2.0 - 1.0;
    vec3 Q1  = dFdx(WorldPos);
    vec3 Q2  = dFdy(WorldPos);
    vec2 st1 = dFdx(TexCoords);
    vec2 st2 = dFdy(TexCoords);
    vec3 T   = normalize(Q1 * st2.t - Q2 * st1.t);
    vec3 B   = -normalize(cross(N, T));
    return normalize(mat3(T, B, N) * tangentNormal);
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH  = max(dot(N, H), 0.0);
    float denom  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main()
{
    vec4  baseColor = U_BaseColor * VertexColor * texture(U_AlbedoTexture, TexCoords);
    vec3  albedo    = baseColor.rgb;
    vec4  mr        = texture(U_MetallicRoughnessTexture, TexCoords);
    float roughness = clamp(U_Roughness * mr.g, 0.04, 1.0);
    float metallic  = clamp(U_Metallic * mr.b, 0.0, 1.0);
    float ao        = texture(U_OcclusionTexture, TexCoords).r;

    vec3 N  = GetNormal();
    vec3 V  = normalize(U_ViewPos - WorldPos);
    vec3 R  = reflect(-V, N);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 Lo = vec3(0.0);
    for (int i = 0; i < 4; ++i)
    {
        vec3  L           = normalize(lightPositions[i] - WorldPos);
        vec3  H           = normalize(V + L);
        float distance    = length(lightPositions[i] - WorldPos);
        vec3  radiance    = lightColors[i] / (distance * distance);
        float NDF         = DistributionGGX(N, H, roughness);
        float G           = GeometrySmith(N, V, L, roughness);
        vec3  F           = FresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3  specular    = NDF * G * F / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001);
        vec3  kD          = (vec3(1.0) - F) * (1.0 - metallic);
        Lo += (kD * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
    }

    vec3 F  = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = textureLod(U_IrradianceMap, N, 0.0).rgb * albedo;
    const float MAX_REFLECTION_LOD = 4.0;
    vec3 prefiltered = textureLod(U_PrefilterMap, R, roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf        = texture(U_BrdfLUTMap, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular    = prefiltered * (F * brdf.x + brdf.y);

    vec3 color = (kD * diffuse + specular) * ao + Lo;
    if (U_HasEmissiveTexture != 0)
        color += texture(U_EmissiveTexture, TexCoords).rgb;

    color     = color / (color + vec3(1.0));
    color     = pow(color, vec3(1.0 / 2.2));
    // Opaque pipeline: texture alpha is often a mask, not coverage.
    FragColor = vec4(color, 1.0);
    if (U_DebugView == 1)
        FragColor = vec4(pow(albedo, vec3(1.0 / 2.2)), 1.0);
    else if (U_DebugView == 2)
        FragColor = vec4(N * 0.5 + 0.5, 1.0);
}
