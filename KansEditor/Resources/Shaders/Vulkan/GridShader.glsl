#version 450 core 
#pragma stage : vert
// Grid Shader
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TextureCroods;

layout(location = 0) out vec2 v_TexCoord;

layout(set = 0, binding = 0) readonly buffer _unused_name_perframe
{
    mat4 U_ViewProjection;
    mat4 U_Transform;
};

void main()
{
    vec4 position = U_ViewProjection* U_Transform * vec4(a_Position, 1.0);
    gl_Position = position;

    v_TexCoord = a_TextureCroods;
}

#version 450 core 
#pragma stage : frag

layout(location = 0) out vec4 color;
layout(location = 0) in vec2 v_TexCoord;


layout(push_constant) uniform Settings
{
	float Scale;
	float Size;
} U_Settings;


float grid(vec2 st, float res)
{
    vec2 grid = fract(st);
    return step(res, grid.x) * step(res, grid.y);
}

void main()
{
	float x = grid(v_TexCoord * U_Settings.Scale, U_Settings.Size);
	color = vec4(vec3(0.9), 0.5) * (1.0 - x);
	
	if (color.a == 0.0)
		discard;
		
}
