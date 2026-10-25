#version 450
#include <Kans3D/Engine.glsl>
#include <Kans3D/VertexInput.glsl>

layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_UV;

void main()
{
    vec4 world  = u_Object.Transform * vec4(a_Position, 1.0);
    v_WorldPos  = world.xyz / world.w;
    v_Normal    = mat3(u_Object.NormalMatrix) * a_Normal;
    v_UV        = a_UV;
    gl_Position = u_Frame.ViewProjection * world;
}
