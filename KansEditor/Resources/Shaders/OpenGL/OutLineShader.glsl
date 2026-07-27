#type vertex
#version 430 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TextureCroods;
layout(location = 3) in vec4 a_BaseColor;
layout(location = 4) in vec4 a_Tangent;


layout(location = 0) out vec2 V_TexCroods; 
layout(location = 1) out vec4 V_BaseColor;
layout(location = 2) out float V_Fade_rate;
uniform mat4 U_ViewProjection;
uniform mat4 U_Transform;
uniform vec3 U_ViewPos;
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
float OutlineAlpha(float dist, float k, float n) {
    float d_over_k = dist / k;
    float alpha = 1.0 / (1.0 + pow(d_over_k, n));
    return alpha;
}
void main()
{
    //pass Data
    V_TexCroods = a_TextureCroods;
    V_BaseColor = a_BaseColor;

    //（Moldle-1）T
	vec3 Normal =  mat3(U_ViewProjection)*mat3(transpose(inverse(U_Transform)))*a_Normal;
    Normal = normalize(Normal);
	vec3 tangent = mat3(U_ViewProjection)*mat3(transpose(inverse(U_Transform)))*a_Tangent.xyz;
    //tangent = normalize(tangent - dot(Normal,tangent)*Normal);
    tangent = normalize(tangent);
	vec3 bitangent =  cross(Normal,tangent)*a_Tangent.w;
    bitangent = normalize(bitangent);
	mat3 TBN = mat3(tangent, bitangent, Normal);
	Normal = TBN*a_BaseColor.rgb;
    //Normal = mat3(U_ViewProjection)*mat3(transpose(inverse(U_Transform)))*a_Normal;
    //处理背面遮挡问题
    Normal.z = -0.5;
    Normal = normalize(Normal);

	//s_Position is in the screen space
    vec4 s_Position = U_ViewProjection*U_Transform*vec4(a_Position, 1.0);
    vec4 world_pos = U_Transform*vec4(a_Position, 1.0);
	//gl_Position = vec4(s_Position.xy + Normal.xy*s_Position.w*0.004*(0.8+0.2*noise(a_Position.x+a_Position.y+a_Position.z)),s_Position.z,s_Position.w);
    float dist = distance(world_pos.xyz/world_pos.w, U_ViewPos);
    V_Fade_rate = OutlineAlpha(dist,10, 5.5);
    
    gl_Position = vec4(s_Position.xy + Normal.xy*s_Position.w*0.004*V_Fade_rate,s_Position.z,s_Position.w);
    
}


#type fragment
#version 430 core
layout(location = 0) out vec4 O_Color;

layout(location = 0) in vec2  V_TexCroods; 
layout(location = 1) in vec4  V_BaseColor;
layout(location = 2) in float V_Fade_rate; 
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
    vec4 Texcolor = texture(material.U_DiffuseTexture,vec2(V_TexCroods.x,1.0-V_TexCroods.y));
    float Bright = 0.299*Texcolor.r + 0.587*Texcolor.g + 0.114*Texcolor.b;


    //O_Color =vec4(Texcolor.rgb*Bright,1.0);
    O_Color =vec4(vec3(0.0,0.0,0.0),V_Fade_rate);
    //O_Color = vec4(V_Fade_rate);
}
