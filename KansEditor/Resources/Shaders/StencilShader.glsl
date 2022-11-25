#type vertex
#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TextureCroods;
layout(location = 3) in vec4 a_BaseColor;
uniform mat4 U_ViewProjection;
uniform mat4 U_Transform;
out vec3 V_Normal;
out vec2 V_TexCroods;
out vec4 V_BaseColor;
void main()
{
	// gl_Position is in the screen space
	gl_Position = U_ViewProjection*U_Transform*vec4(a_Position, 1.0);

	V_TexCroods = a_TextureCroods;
	V_BaseColor = a_BaseColor;

	//the V_Normal is in worldspcae
	V_Normal = mat3(transpose(inverse(U_Transform)))*a_Normal;
	
}

#type fragment
#version 330 core
layout(location = 0) out vec4 color;

//geometry attribute
in vec2 V_TexCroods;
in vec4 V_BaseColor;

//World space attribute
in vec3 V_Normal;
void main()
{
	// properties

	color = vec4(0.89, 0.67, 0.52, 1);
	//Transform to light space
	//color = vec4(color.r*0.299+color.g*0.731+color.b*0.121);
}