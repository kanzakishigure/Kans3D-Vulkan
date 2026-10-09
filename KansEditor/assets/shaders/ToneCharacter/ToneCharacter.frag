#version 450
// Genshin-style character shading (ramp shadows selected by the light map,
// stepped specular), lit by the scene's first point light.
//
// Textures: u_Diffuse (base color), u_LightMap (*_Light: R specular type,
// G shadow/AO mask, B specular mask, A ramp row), u_RampMap (*_Ramp).
// Like the legacy shader, shading is computed on the stored (gamma-encoded)
// texture values and written without further encoding, and textures are
// sampled the way the legacy loader configured them (see LegacySample).
#include <Kans3D/Engine.glsl>

layout(set = 1, binding = 0, std140) uniform ToneParameters
{
    float RampShadowRange;
    float RampAOShadowRange;
    float ShadowIntensity;
    float BrightIntensity;
    float AOIntensity;
    float CharacterIntensity;
    float RampIntensity;
    float SpecularIntensity1; // silk
    float SpecularIntensity2; // black stockings
    float SpecularIntensity3; // ropes
    bool  LightMapFlipV;      // the legacy shader sampled the light map at (u, 1 - v)
    bool  LightMapIsData;     // set when the light map is imported with usage: data
} u_Tone;
layout(set = 1, binding = 1) uniform sampler2D u_Diffuse;
layout(set = 1, binding = 2) uniform sampler2D u_LightMap;
layout(set = 1, binding = 3) uniform sampler2D u_RampMap;

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

// Color textures are decoded from sRGB when sampled; the legacy math expects
// the stored (encoded) values.
vec3 Encode(vec3 linear)
{
    return mix(linear * 12.92, 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), linear));
}

// The legacy loader created textures without mipmaps, with linear minification,
// nearest magnification and repeat wrapping; imported textures have mipmaps and
// linear magnification, which would blend ramp rows and light-map regions.
vec4 LegacySample(sampler2D image, vec2 uv)
{
    if (textureQueryLod(image, uv).y > 0.0)
        return textureLod(image, uv, 0.0);
    ivec2 size  = textureSize(image, 0);
    ivec2 texel = clamp(ivec2(floor(fract(uv) * vec2(size))), ivec2(0), size - 1);
    return texelFetch(image, texel, 0);
}

// Stored 8-bit value of a color texture channel.
vec3 Stored(vec3 sampled) { return round(Encode(sampled) * 255.0) / 255.0; }

vec3 RampRow(float u, float v) { return pow(Stored(LegacySample(u_RampMap, vec2(u, v)).rgb), vec3(1.0 / 2.2)); }

void main()
{
    vec3 norm     = normalize(v_Normal);
    vec3 viewDir  = normalize(u_Frame.CameraPosition.xyz - v_WorldPos);
    vec3 lightDir = normalize(u_Frame.LightPositions[0].xyz - v_WorldPos);

    vec4 baseColor = vec4(Stored(LegacySample(u_Diffuse, v_UV).rgb), 1.0);
    vec2 lightUV   = u_Tone.LightMapFlipV ? vec2(v_UV.x, 1.0 - v_UV.y) : v_UV;
    vec4 lightMap  = LegacySample(u_LightMap, lightUV);
    if (!u_Tone.LightMapIsData)
        lightMap.rgb = Stored(lightMap.rgb);

    float lambert = max(0.0, dot(lightDir, norm)) * 0.5 + 0.5;
    float NoV     = dot(viewDir, norm);

    // Material region from the light map.
    float shadowAOMask = smoothstep(0.2, 0.6, clamp(lightMap.g * 2.0, 0.0, 1.0));
    shadowAOMask       = mix(0.5, 1.0, shadowAOMask);
    float rampU        = clamp(lambert * shadowAOMask * (1.0 / u_Tone.RampShadowRange - 0.03), 0.0, 0.98);

    vec3 rampShadowColor1 = RampRow(rampU, 0.05); // tights
    vec3 rampShadowColor2 = RampRow(rampU, 0.15); // hard / emission / specular / silk
    vec3 rampShadowColor3 = RampRow(rampU, 0.25); // metal
    vec3 rampShadowColor4 = RampRow(rampU, 0.35); // skin
    vec3 rampShadowColor5 = RampRow(rampU, 0.45); // soft / common

    vec3 tightsRamp     = step(abs(lightMap.a - 0.7), 0.05) * rampShadowColor5;
    vec3 skinRamp       = step(abs(lightMap.a - 1.0), 0.05) * rampShadowColor4;
    vec3 softCommonRamp = step(abs(lightMap.a - 0.5), 0.05) * rampShadowColor3;
    vec3 hardSilkRamp   = step(abs(lightMap.a - 0.3), 0.1) * rampShadowColor2;
    vec3 metalRamp      = step(abs(lightMap.a - 0.0), 0.05) * rampShadowColor1;
    vec3 rampColor      = pow(skinRamp + tightsRamp + softCommonRamp + hardSilkRamp + metalRamp, vec3(2.2));

    vec3  baseMapShadowed = mix(baseColor.rgb * rampColor, baseColor.rgb, shadowAOMask);
    baseMapShadowed       = mix(baseColor.rgb, baseMapShadowed, u_Tone.AOIntensity);
    float isLightSide     = shadowAOMask * step(u_Tone.RampShadowRange, lambert);
    vec3  shadowWithAO =
        mix(baseMapShadowed, baseColor.rgb * rampColor, u_Tone.RampAOShadowRange) * u_Tone.ShadowIntensity;
    vec3 diffuse = mix(shadowWithAO, u_Tone.BrightIntensity * baseMapShadowed, isLightSide * u_Tone.RampIntensity) *
                   u_Tone.CharacterIntensity * u_Frame.LightDiffuse[0].rgb;

    // Stepped specular by material type (light map R).
    int   specularType = int(lightMap.r * 255.0);
    float specularMask = lightMap.b;
    float intensity    = 0.0;
    if (specularType < 10)
        intensity = u_Tone.SpecularIntensity1;
    else if (specularType > 15 && specularType < 35)
        intensity = u_Tone.SpecularIntensity2;
    else if (specularType > 125 && specularType < 135)
        intensity = u_Tone.SpecularIntensity3;
    vec3 specular = clamp(NoV, 0.0, 1.0) * intensity * vec3(specularMask) * baseColor.rgb;

    o_Color = vec4(specular + diffuse, 1.0);
}
