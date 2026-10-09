#version 450
// Sample material shader: cel shading with a two-tone or ramp-texture shadow
// and a rim light, lit by the scene's first point light.
//
// Material data lives in descriptor set 1; the material editor lists these
// members and samplers. Parameters left at zero use the defaults noted below.
#include <Kans3D/Engine.glsl>

layout(set = 1, binding = 0, std140) uniform ToonParameters
{
    vec4  BaseColor;       // zero: white
    vec4  ShadowColor;     // zero: (0.65, 0.6, 0.7)
    vec4  RimColor;        // alpha is the rim strength; zero: no rim
    float ShadowThreshold; // zero: 0.5
    float ShadowSoftness;  // zero: 0.05
    float RimPower;        // zero: 4
    float RampBlend;       // 0: two-tone shadow, 1: shadow from u_Ramp
} u_Toon;
layout(set = 1, binding = 1) uniform sampler2D u_Albedo; // unassigned: white
layout(set = 1, binding = 2) uniform sampler2D u_Ramp;   // sampled by half-Lambert in U

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

float Or(float value, float fallback) { return value != 0.0 ? value : fallback; }
vec4  Or(vec4 value, vec4 fallback) { return any(notEqual(value, vec4(0.0))) ? value : fallback; }

void main()
{
    vec3 N = normalize(v_Normal);
    vec3 L = normalize(u_Frame.LightPositions[0].xyz - v_WorldPos);
    vec3 V = normalize(u_Frame.CameraPosition.xyz - v_WorldPos);

    float halfLambert = dot(N, L) * 0.5 + 0.5;
    float threshold   = Or(u_Toon.ShadowThreshold, 0.5);
    float softness    = Or(u_Toon.ShadowSoftness, 0.05);
    float lit         = smoothstep(threshold - softness, threshold + softness, halfLambert);
    vec3  shadow      = Or(u_Toon.ShadowColor, vec4(0.65, 0.6, 0.7, 1.0)).rgb;
    vec3  twoTone     = mix(shadow, vec3(1.0), lit);
    vec3  ramp        = texture(u_Ramp, vec2(halfLambert, 0.5)).rgb;
    vec3  tone        = mix(twoTone, ramp, clamp(u_Toon.RampBlend, 0.0, 1.0));

    vec4  albedo = Or(u_Toon.BaseColor, vec4(1.0)) * texture(u_Albedo, v_UV);
    float rim    = pow(1.0 - max(dot(N, V), 0.0), Or(u_Toon.RimPower, 4.0));
    vec3  color  = albedo.rgb * tone + u_Toon.RimColor.rgb * u_Toon.RimColor.a * rim;

    // Textures are sampled as linear values; encode for the LDR scene target.
    o_Color = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
