#type vertex
#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TextureCroods;
layout(location = 3) in vec4 a_BaseColor;
layout(location = 3) in vec3 a_Tangent;
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
	sampler2D U_RampTexture0;
	sampler2D U_RampTexture1;
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

uniform float U_DividLineH;
uniform float U_DividLineM;
uniform float U_DividLineL;
uniform float U_Roughness;
uniform float U_DividLineSpec;
uniform float U_DividLineDeepDark;
uniform vec3 U_DarkColor;


uniform float U_ShadowAttWeight;
uniform float atten;
//geometry attribute
in vec2 V_TexCroods;
in vec4 V_BaseColor;

//World space attribute
in vec3 V_FragPos;
in vec3 V_Normal;

//camera attribute
uniform vec3 U_ViewPos;

float sigmoid(float x, float center, float sharp) 
{
	float s;
	s = 1.0 / (1.0 + pow(100000, (-3.0 * sharp * (x - center))));
	return s;
}
vec4 HDRColormap(vec3 hdrColor)
{
	const float gamma = 2.2;
    // Reinhard色调映射
    vec3 mapped = hdrColor / (hdrColor + vec3(1.0));
    // Gamma校正
    mapped = pow(mapped, vec3(1.0 / gamma));
	return vec4(mapped, 1.0);
}
//GGX NDF
float D_GGX(float a2, float NoH) 
{
		float d = (NoH * a2 - NoH) * NoH + 1;
		return a2 / (PI * d * d);
}
// Official HSV to RGB conversion 
vec3 hsv2rgb( in vec3 c )
{
    vec3 rgb = clamp( abs(mod(c.x*6.0+vec3(0.0,4.0,2.0),6.0)-3.0)-1.0, 0.0, 1.0 );

    return c.z * mix( vec3(1.0), rgb, c.y);
}
// Smooth HSV to RGB conversion 
// https://www.shadertoy.com/view/MsS3Wc
vec3 hsv2rgb_smooth( in vec3 c )
{
    vec3 rgb = clamp( abs(mod(c.x*6.0+vec3(0.0,4.0,2.0),6.0)-3.0)-1.0, 0.0, 1.0 );

    rgb = rgb*rgb*(3.0-2.0*rgb); // cubic smoothing 

    return c.z * mix( vec3(1.0), rgb, c.y);
}
vec3 rgb2hsb( in vec3 c )
{
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz),vec4(c.gb, K.xy),step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r),vec4(c.r, p.yzx),step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)),d / (q.x + e),q.x);
}
float ndc2Normal(float x) {
     return x * 0.5 + 0.5;
}
void main()
{
	// properties
    vec3 norm = normalize(V_Normal);

    vec3 viewDir = normalize(U_ViewPos - V_FragPos );

	vec3 lightdir = normalize(pointLight.Position - V_FragPos);

	float NoL =  max(dot(lightdir,norm),0.0);

	vec3 HalfVector = normalize(lightdir + viewDir);
	float NoH = dot(HalfVector,norm);

    vec4 Texcolor = texture2D(material.U_DiffuseTexture,V_TexCroods);
    vec4 rampcolor = texture2D(material.U_RampTexture0,vec2(0.0,NoL));

    
    O_Color = smoothstep(0.2,0.5,NoL)*( Texcolor-vec4(U_DarkColor,1.0)) + vec4(U_DarkColor,1.0);
	O_Normal = Texcolor;
	
	{
		//代码归档
		////////////////////////////////////////////////////////
		//在HSV空间调整亮度级别
		/*
			vec3 TexcolorHSV = rgb2hsb(Texcolor.rgb);	
			Intensity = clamp(Intensity,0.05,0.6);
			TexcolorHSV.b +=Intensity;
			Texcolor = vec4(hsv2rgb_smooth(TexcolorHSV),1.0);
		*/
	}


}