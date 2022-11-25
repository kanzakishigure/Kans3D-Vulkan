#type vertex
#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TextureCroods;
layout(location = 3) in vec4 a_BaseColor;
layout(location = 4) in vec3 a_Tangent;
uniform mat4 U_ViewProjection;
uniform mat4 U_Transform;
out vec3 V_Normal;
out vec2 V_TexCroods;
out vec4 V_BaseColor;
out vec3 V_FragPos;
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
layout(location = 0) out vec4 O_Color;
layout(location = 1) out vec4 O_Normal;
#define PI 3.1415926

struct Material
{
	sampler2D U_DiffuseTexture;
	sampler2D U_SpecularTexture;
	sampler2D U_NormalTexture;
	sampler2D U_EmissionTexture;
	float U_Shininess;
};

struct DirLight
{
	vec3 LightDir;
	vec3 Diffuse_Intensity;
	vec3 Specular_Intensity;
	vec3 Ambient_Intensity;
};
struct PointLight
{
	vec3 Position;
	vec3 Diffuse_Intensity;
	vec3 Specular_Intensity;
	vec3 Ambient_Intensity;
};

//shading point Material
uniform Material material;
//scene info 
uniform DirLight dirLight;
uniform PointLight pointLight;
//geometry attribute
in vec2 V_TexCroods;
in vec4 V_BaseColor;

//World space attribute
in vec3 V_FragPos;
in vec3 V_Normal;

//camera attribute
uniform vec3 U_ViewPos;



vec3 CalcDirLight(DirLight dirLight,vec3 norm,vec3 viewdir)
{

	vec3 lightDir = normalize(-dirLight.LightDir);
	//difusseShadingCoefficient
	float diff = max(dot(norm, lightDir), 0.0);
	//SpecularShadingCoefficient
	vec3 reflectDir = reflect(-lightDir, norm);
	float spec = pow(max(dot(reflectDir,viewdir),0.0),material.U_Shininess);
	//Ambient
	vec3 ambient  = dirLight.Ambient_Intensity*texture2D(material.U_DiffuseTexture,V_TexCroods).rgb;
	//Difusse
	vec3 diffuse  = diff*dirLight.Diffuse_Intensity*texture2D(material.U_DiffuseTexture,V_TexCroods).rgb;
	//Specular
	vec3 specular = spec*dirLight.Specular_Intensity*texture2D(material.U_SpecularTexture,V_TexCroods).rgb;
	return (ambient + diffuse + specular);
}
vec3 CalcPointLight(PointLight pointLight,vec3 norm,vec3 fragPos,vec3 viewDir)
{
	vec3 lightDir = normalize(pointLight.Position - fragPos);
    // diffuse shading
    float diff = max(dot(norm, lightDir), 0.0);
    // specular shading
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.U_Shininess);
    // attenuation
    float distance = length(pointLight.Position - fragPos);
    float attenuation = 1.0 /(4*PI*distance*distance);    
    // combine results
    vec3 ambient = pointLight.Ambient_Intensity * texture2D(material.U_DiffuseTexture,V_TexCroods).rgb;
    vec3 diffuse = pointLight.Diffuse_Intensity * diff * texture2D(material.U_DiffuseTexture,V_TexCroods).rgb;
    vec3 specular = pointLight.Specular_Intensity * spec * texture2D(material.U_SpecularTexture,V_TexCroods).rgb;
    
    //diffuse *= attenuation;
    specular *= attenuation;
    return (ambient + diffuse + specular);
}
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

	O_Color = vec4(result,1.0);
	O_Normal = vec4(V_Normal,1.0);
	//Transform to light space

	//O_Normal = vec4(result.r*0.2126+result.g*0.7152+result.b*0.0722);
}