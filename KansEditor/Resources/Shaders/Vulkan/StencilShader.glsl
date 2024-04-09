#version 450 core 
#pragma stage : vert

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TextureCroods;
layout(location = 3) in vec4 a_BaseColor;

layout(location = 0) out vec3 V_Normal;
layout(location = 1) out vec2 V_TexCroods;
layout(location = 2) out vec4 V_BaseColor;

layout(set = 0, binding = 0) readonly buffer _unused_name_perframe
{
    mat4 U_ViewProjection;
    mat4 U_Transform;
};


void main()
{
	// gl_Position is in the screen space
	gl_Position = U_ViewProjection*U_Transform*vec4(a_Position, 1.0);

	V_TexCroods = a_TextureCroods;
	V_BaseColor = a_BaseColor;

	//the V_Normal is in worldspcae
	V_Normal = mat3(transpose(inverse(U_Transform)))*a_Normal;
	
}

#version 450 core 
#pragma stage : frag
layout(location = 0) out vec4 color;

layout(location = 0) in vec3 V_Normal;
layout(location = 1) in vec2 V_TexCroods;
layout(location = 2) in vec4 V_BaseColor;

void main()
{
	color = vec4(0.89, 0.67, 0.52, 1);
}