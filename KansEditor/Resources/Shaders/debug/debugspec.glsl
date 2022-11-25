#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TextureCroods;
layout(location = 3) in vec4 a_BaseColor;
layout(location = 3) in vec3 a_Tangent;
layout(location = 4) in vec3 a_Bitangent;

uniform mat4 U_ViewProjection;
uniform mat4 U_Transform;



out VS_OUT {
    vec3 normal;
} vs_out;
void main()
{


    vec3 normal =  mat3(transpose(inverse(U_Transform)))*a_Normal;
	vec3 tangent =  mat3(U_Transform)*a_Tangent;
	vec3 bitangent =  normalize(cross(normal,tangent));
	mat3 tangentTransform = mat3(tangent, bitangent, normal);


	vec4 bakedNormal = vec4(tangentTransform*a_BaseColor.rgb,1.0);
	bakedNormal = U_ViewProjection*bakedNormal;
    bakedNormal = normalize(bakedNormal);
	// gl_Position is in the screen space
    vec4 Normal =  U_ViewProjection*vec4(mat3(transpose(inverse(U_Transform)))*a_Normal,1.0);
	Normal = normalize(Normal);

    vec4 o_Position = U_ViewProjection*U_Transform*vec4(a_Position, 1.0);
    gl_Position = o_Position;
    
    //vertex Normal
    vs_out.normal = Normal.xyz;
    //baked Normal
    vs_out.normal = bakedNormal.xyz;
    
}