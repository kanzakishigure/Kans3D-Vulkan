#type vertex
#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TextureCroods;
layout(location = 3) in vec4 a_BaseColor;
layout(location = 4) in vec3 a_Tangent;
layout(location = 5) in vec3 a_Bitangent;
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
	//��Moldle-1��T
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
	sampler2D U_ToneRampMap;
	float U_Shininess;
};



//shading point Material
uniform Material material;

//geometry attribute
in vec2 V_TexCroods;
in vec4 V_BaseColor;

//World space attribute
in vec3 V_FragPos;
in vec3 V_Normal;

//Test data
uniform float U_PerlinBias;
uniform vec3 U_Effect;

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
	// properties
    vec3 norm = normalize(V_Normal);
	vec3 color = V_FragPos*0.5+0.5;

	float pin = distance(vec3(0.0,0.0,0.0),V_FragPos);
	float pout = noise(pin)*U_PerlinBias;

	vec3 pcolor =  vec3(noise(V_FragPos.x)*U_Effect.x,noise(V_FragPos.y)*U_Effect.y,noise(V_FragPos.z)*U_Effect.z);
	O_Color = texture2D(material.U_ToneRampMap,vec2(pout,0.5));
	//O_Color = vec4(pcolor,1.0);
	//O_Color = vec4(color,1.0);
	//O_Normal = vec4(V_Normal,1.0);


	//Transform to light space
	//O_Normal = vec4(result.r*0.2126+result.g*0.7152+result.b*0.0722);
}