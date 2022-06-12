#type vertex
#version 330 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec4 a_Color;
layout(location = 2) in vec2 a_TextureCroods;
layout(location = 3) in float a_TexIndex;
layout(location = 4) in float a_TilingFactor;
uniform mat4 U_ViewProjection;
out vec2 V_TexCroods;
out vec4 V_Color;
out float V_TexIndex;
out float V_TilingFactor;
void main()
{
	gl_Position = U_ViewProjection *1.0* vec4(a_Position, 1.0);
	V_TexCroods = a_TextureCroods;
	V_Color = a_Color;
	V_TexIndex = a_TexIndex;
	V_TilingFactor = a_TilingFactor;
}

#type fragment
#version 330 core
layout(location = 0) out vec4 color;

uniform sampler2D[32] U_TextureSample;

in vec4 V_Color;
in vec2 V_TexCroods;
in float V_TexIndex;
in float V_TilingFactor;
void main()
{
	int index = int(V_TexIndex);
	color = V_Color*texture2D(U_TextureSample[index],V_TexCroods*V_TilingFactor);
}