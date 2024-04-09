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
	//（Moldle-1）T
	//the normal is in worldspcae
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
	sampler2D U_ToneLightMap;
	float U_Shininess;
	sampler2D U_ToneRampMap;
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
uniform vec4 U_DarkColor4;


uniform float U_ShadowAttWeight;
uniform float atten;
uniform float U_Debug;
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
    // 曝光色调映射
    vec3 mapped = vec3(1.0) - exp(-hdrColor * 1.0);
    // Gamma校正
    mapped = pow(mapped, vec3(1.0 / gamma));
	return vec4(mapped, 1.0);
}
//GGX NDF
float D_GGX(float a2, float NoH) 
{
		float d = (NoH * a2 - NoH) * NoH + 1;
		return a2 / (3.14159 * d * d);
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
vec3 Fresnel_extend(float VoN, vec3 rF0) 
{
	return rF0 + (1 - rF0) * pow((1 - VoN),8.0);
}
void main()
{
	// properties
	float U_BoundSharp = 9.5 * pow(U_Roughness - 1,2.0) + 0.5;
    vec3 norm = normalize(V_Normal);
    vec3 viewDir = normalize(U_ViewPos - V_FragPos );
	vec3 lightdir = normalize(pointLight.Position - V_FragPos);
	vec3 HalfVector = normalize(lightdir + viewDir);

	vec4 Texcolor = texture2D(material.U_DiffuseTexture,V_TexCroods);

	float NoL = max(dot(lightdir,norm),0.0);
	float NoV = dot(viewDir,norm);
	NoV  = pow(NoV,atten);

	float NoH = pow(dot(HalfVector,norm),2) + U_ShadowAttWeight * (atten - 1);

	//diffuse
	float AO = texture2D(material.U_ToneLightMap,V_TexCroods).g;
	float Noloffset = texture2D(material.U_ToneLightMap,V_TexCroods).b;
	float Metalmask = texture2D(material.U_ToneLightMap,V_TexCroods).r;
	vec4 diffuse = texture2D(material.U_ToneRampMap,vec2(0,max(NoL,0.0)));
	diffuse = mix(Texcolor,diffuse,AO);
	//diffuse = NoL*AO < r ? vec4(texture2D(material.U_ToneRampMap,vec2(0,NoL*AO)).rgb,1.0): vec4(1.0);
	//diffuse = mix(Texcolor,diffuse,diffuse.g);
	//Spec
	float spec = pow(NoH,atten);
	vec4 specColor =  texture2D(material.U_ToneRampMap,vec2(0,max(spec,0.0)));
	specColor = Metalmask*mix(vec4(vec3(0.0),1.0),specColor,Noloffset);

	//Fresnel 
	float f = pow(1-NoV,atten);

	O_Color = diffuse*Texcolor*vec4(pointLight.Diffuse_Intensity,1.0);
	//+ Texcolor*specColor*vec4(pointLight.Specular_Intensity,1.0)
	//specColor*vec4(pointLight.Specular_Intensity,1.0);
	//O_Color = specColor;
	//O_Color = O_Color + O_Color*f;
	//O_Color = vec4(vec3(f),1.0);
	//O_Color = vec4(vec3(AO),1.0);
	//O_Normal  = vec4(V_Normal,1.0);
	//O_Normal = vec4( V_BaseColor*0.5 + vec4(vec3(0.5),1.0));
	//O_Normal = vec4(vec3(r),1.0);
	//O_Normal =  vec4(texture2D(material.U_ToneRampMap,vec2(0,NoL*AO)).rgb,1.0)*Texcolor*vec4(pointLight.Diffuse_Intensity,1.0) ;
	diffuse = texture2D(material.U_ToneRampMap,vec2(0,max(NoL,0.0)));
	diffuse = mix(Texcolor,diffuse,Metalmask);
	O_Normal = diffuse*Texcolor*vec4(pointLight.Diffuse_Intensity,1.0) ;
	//O_Color = vec4(U_DarkColor,1.0);
	O_Normal = U_DarkColor4;
	//O_Normal = Texcolor;
	//
	//O_Normal = specColor;
	{
		//代码归档
		///////////////////////////
		//O_Color = smoothstep(0.2,0.5,NoL)*( Texcolor-vec4(U_DarkColor,1.0)) + vec4(U_DarkColor,1.0);
		///////////////////////////
		//在HSV空间调整亮度级别
		/*
			vec3 TexcolorHSV = rgb2hsb(Texcolor.rgb);	
			Intensity = clamp(Intensity,0.05,0.6);
			TexcolorHSV.b +=Intensity;
			Texcolor = vec4(hsv2rgb_smooth(TexcolorHSV),1.0);
		*/
	}


}