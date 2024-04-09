#type vertex
#version 330 core
layout(location = 0) in vec3 a_Position;


uniform mat4 U_ViewProjection;
uniform mat4 U_Transform;

void main()
{
	// gl_Position is in the screen space
	gl_Position = U_ViewProjection*U_Transform*vec4(a_Position, 1.0);

	V_TexCroods = a_TextureCroods;
	V_BaseColor = a_BaseColor;

	//the V_Normal is in worldspcae
	//（Moldle-1）T
	V_Normal = mat3(transpose(inverse(U_Transform)))*a_Normal;
	//Get the position in worldspcae
	vec4 pos = U_Transform*vec4(a_Position, 1.0);
	V_FragPos = vec3(pos.xyz)/pos.w;
}

#type fragment
#version 330 core
layout(location = 1) out vec4 O_UUIDMap;

void main()
{
	// properties
    vec3 norm = normalize(V_Normal);
    vec3 viewDir = normalize(U_ViewPos - V_FragPos);

	vec3 result = vec3(0.0);
	//CalcDirLight
	result += CalcDirLight(dirLight,norm,viewDir);
	//CalcPointLight
	result += CalcPointLight(pointLight,norm,V_FragPos,viewDir);
	
	result = vec3(1.0);
	O_Color = vec4(result,1.0);

}