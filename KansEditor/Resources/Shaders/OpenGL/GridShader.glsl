#type vertex
#version 330 core
// Grid Shader
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec2 a_TextureCroods;

out vec2 v_TexCoord;

uniform mat4 U_ViewProjection;
uniform mat4 U_Transform;

void main()
{
    vec4 position = U_ViewProjection* U_Transform * vec4(a_Position, 1.0);
    gl_Position = position;

    v_TexCoord = a_TextureCroods;
}

#type fragment
#version 330 core

layout(location = 0) out vec4 color;

struct Settings
{
	float Scale;
	float Size;
};
uniform Settings U_Settings;

in vec2 v_TexCoord;

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
