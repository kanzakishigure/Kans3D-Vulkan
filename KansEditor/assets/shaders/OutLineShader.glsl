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
out vec4 V_BaseColor;
float rand(float x)
{
    float y = fract(sin(x)*100000.0);
    return y;
}
float noise(float x)
{
    float i = floor(x); 
    float f = fract(x);  
    float u = f * f * (3.0 - 2.0 * f ); // custom cubic curve
    float y = mix(rand(i), rand(i + 1.0), u); // using it in the interpolation
    return y ;
}

void main()
{
    //pass Data
    V_TexCroods = a_TextureCroods;
    V_BaseColor = a_BaseColor;


    //（Moldle-1）T
	vec3 Normal =  mat3(U_ViewProjection)*mat3(U_ViewProjection)*mat3(transpose(inverse(U_Transform)))*a_Normal;
    Normal = normalize(Normal);
	vec3 tangent = mat3(U_ViewProjection)*mat3(U_Transform)*a_Tangent;
    tangent = normalize(tangent);
	vec3 bitangent =  cross(Normal,tangent);
    bitangent = normalize(bitangent);
	mat3 tangentTransform = mat3(tangent, bitangent, Normal);
	Normal = tangentTransform*a_BaseColor.rgb;
    //Normal = mat3(U_ViewProjection)*mat3(transpose(inverse(U_Transform)))*a_Normal;
    //处理背面遮挡问题
    Normal.z = -0.5;
    Normal = normalize(Normal);

	//s_Position is in the screen space
    vec4 s_Position = U_ViewProjection*U_Transform*vec4(a_Position, 1.0);
	//gl_Position = vec4(s_Position.xy + Normal.xy*s_Position.w*0.004*(0.8+0.2*noise(a_Position.x+a_Position.y+a_Position.z)),s_Position.z,s_Position.w);
    gl_Position = vec4(s_Position.xy + Normal.xy*s_Position.w*0.004,s_Position.z,s_Position.w);
}


#type fragment
#version 330 core
layout(location = 0) out vec4 O_Color;

in vec2 V_TexCroods;
struct Material
{
	sampler2D U_DiffuseTexture;
	sampler2D U_SpecularTexture;
	sampler2D U_NormalTexture;
	sampler2D U_EmissionTexture;
	float U_Shininess;
};

uniform Material material;

vec4 OutLineColor = vec4(1);
void main()
{
    
    vec4 Texcolor = texture2D(material.U_DiffuseTexture,V_TexCroods);
    float Bright = 0.299*Texcolor.r + 0.587*Texcolor.g + 0.114*Texcolor.b;
    O_Color =vec4(Texcolor.rgb*Bright,1.0);
}
