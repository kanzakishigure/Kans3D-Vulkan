#version 450
#include <Kans3D/Engine.glsl>
#include <Kans3D/VertexInput.glsl>
#include "Parameters.glsl"
void main()
{
    vec3 worldNormal = normalize(mat3(u_Object.NormalMatrix) * a_Normal);
    vec3 worldPosition = (u_Object.Transform * vec4(a_Position, 1)).xyz;
    gl_Position = u_Frame.ViewProjection * vec4(worldPosition + worldNormal * p.OutlineWidth, 1);
}
