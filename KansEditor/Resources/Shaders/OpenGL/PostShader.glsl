#type vertex
#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TextureCroods;
layout(location = 3) in vec4 a_BaseColor;
layout(location = 4) in vec3 a_Tangent;
uniform mat4 U_ViewProjection;
uniform mat4 U_Transform;
out vec2 V_TexCroods;
void main()
{
	// gl_Position is in the screen space
	gl_Position = U_ViewProjection*U_Transform*vec4(a_Position, 1.0);


	V_TexCroods = a_TextureCroods;

}

#type fragment
#version 330 core
layout(location = 0) out vec4 color;

//geometry attribute
in vec2 V_TexCroods;
uniform sampler2D ColorAttachment;
void main()
{
	// properties

	color = vec4(0.89, 0.67, 0.52, 1);
	//Transform to light space
	//color = vec4(color.r*0.299+color.g*0.731+color.b*0.121);
}