
#type vertex
#version 330 core


layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TextureCroods;
layout(location = 3) in vec4 a_BaseColor;
layout(location = 4) in vec3 a_Tangent;

uniform  mat4 U_ViewProjection;
uniform  mat4 U_Transform;

out vec3 V_Normal;
out vec2 V_TexCroods;
out vec4 V_BaseColor;
out vec3 V_FragPos;
void main()
{
	// gl_Position is in the screen space
	gl_Position = U_ViewProjection*U_Transform*vec4(a_Position, 1.0);

	//V_TexCroods = a_TextureCroods;
	V_TexCroods = vec2(a_TextureCroods.x,a_TextureCroods.y);
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
	sampler2D U_ToneLightMap;
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

//ToneShader parameter
uniform vec4 U_ShadowMultColor;
uniform vec4 U_DarkShadowMultColor;
uniform float U_ShadowArea;
uniform float U_FixDarkShadow;
uniform float U_ShadowSmooth;
uniform float U_DarkShadowSmooth;
uniform float U_DarkShadowArea;
uniform float U_RampShadowRange; 

uniform vec2 U_RampSkin;
uniform vec2 U_RampTights;
uniform vec2 U_RampSoft;
uniform vec2 U_RampMetal;
uniform vec2 U_RampSilk;

uniform bool U_UseRamp; 
uniform bool U_UseFaceMask;
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

void main()
{
	// properties
    vec3 norm = normalize(V_Normal);
    vec3 viewDir = normalize(U_ViewPos - V_FragPos );
	vec3 lightdir = normalize(pointLight.Position - V_FragPos);
	vec3 HalfVector = normalize(lightdir + viewDir);


	vec4 BaseColor = texture2D(material.U_DiffuseTexture,vec2(V_TexCroods.x,1.0-V_TexCroods.y));
	
    vec4 LightMapColor = texture2D(material.U_ToneLightMap,vec2(V_TexCroods.x,1.0-V_TexCroods.y));
	//Prep
		//lambert(LoN)
		float lambert =  dot(lightdir,norm);
		lambert = lambert*0.5+0.5;
		//lambert = smoothstep(0,0.5,lambert);
		float NoV = dot(viewDir,norm);
		vec3 ShadowColor = BaseColor.rgb * U_ShadowMultColor.rgb;
		vec3 DarkShadowColor = BaseColor.rgb * U_DarkShadowMultColor.rgb;
		vec3 rampShadowColor = vec3(1.0);
	//使用ramp贴图
	if(U_UseRamp)
	{
		
		float rampU = clamp(lambert*(1.0/U_RampShadowRange - 0.03),0.0,1.0);
		
		//从lightMap中读取到对应材质所在区间
		float rampV = 1.0 - LightMapColor.a;
		rampV = LightMapColor.a*0.5;
		//归一化到0.5 -1.0
		rampV = floor(rampV*10.0)/10.0; 
		


		//skin
		vec3 rampShadowColor1 = texture2D(material.U_ToneRampMap,vec2(rampU,0.05)).rgb;
		//tights
		vec3 rampShadowColor2 = texture2D(material.U_ToneRampMap,vec2(rampU,0.15)).rgb;
		//metal
		vec3 rampShadowColor3 = texture2D(material.U_ToneRampMap,vec2(rampU,0.25)).rgb;
		//soft/common
		vec3 rampShadowColor4 = texture2D(material.U_ToneRampMap,vec2(rampU,0.35)).rgb;
		// hard/emission/specular/silk
		vec3 rampShadowColor5 = texture2D(material.U_ToneRampMap,vec2(rampU,0.45)).rgb;
		
		vec3 rampShadowColorCool1 = texture2D(material.U_ToneRampMap,vec2(rampU,0.55)).rgb;
		vec3 rampShadowColorCool2 = texture2D(material.U_ToneRampMap,vec2(rampU,0.65)).rgb;
		vec3 rampShadowColorCool3 = texture2D(material.U_ToneRampMap,vec2(rampU,0.75)).rgb;
		vec3 rampShadowColorCool4 = texture2D(material.U_ToneRampMap,vec2(rampU,0.85)).rgb;
		vec3 rampShadowColorCool5 = texture2D(material.U_ToneRampMap,vec2(rampU,0.95)).rgb;

		vec3 rampmaps[10] = vec3[10](rampShadowColor1,rampShadowColor2,rampShadowColor3,rampShadowColor4,rampShadowColor5,
							rampShadowColorCool1,rampShadowColorCool2,rampShadowColorCool3,rampShadowColorCool4,rampShadowColorCool5);

				
		int index = int(rampV);
		vec3 rampColor = rampmaps[index];
		//vec3 rampColor = texture2D(material.U_ToneRampMap,vec2(lambert,1.0 - (LightMapColor.a*0.45 + 0.55))).rgb;
		float rampvalue =  smoothstep(U_RampShadowRange,lambert,1.0);

		rampShadowColor = rampvalue*BaseColor.rgb + (1-rampvalue)*(BaseColor.rgb*rampColor);

		DarkShadowColor = rampShadowColor*U_ShadowMultColor.rgb;
		ShadowColor = rampShadowColor*U_ShadowMultColor.rgb;
	}
	if(U_UseFaceMask)
	{

	}


	float SWeight = (LightMapColor.g * V_BaseColor.r + lambert) * 0.5 + 1.125;
	//当sfactor为0时为原色，否则为一级阴影色
	// 使用sfactor作为基础色和阴影色的混合系数
	float SFactor = floor(SWeight - U_ShadowArea);
	vec3 ShallowShadowColor = SFactor * BaseColor.rgb + (1 - SFactor) * ShadowColor.rgb;
	
	//当sfactor为0时为一级阴影色，否则为二级阴影色
	SFactor = floor(SWeight - U_DarkShadowArea);	
	// 使用sfactor作为一级阴影色和二级阴影色的混合系数
	//U_FixDarkShadow * ShadowColor + (1 - U_FixDarkShadow) * ShallowShadowColor
	//本质还是插值，决定了ShadowColor(用户定义)和ShadowColor(一级阴影色)（ShadowColor和Basecolor插值得到）的占比
	// DarkShadowColor 由{(ShallowShadowColor) 和ShadowColor（一级阴影色）的阴影色插值结果} 与 {DarkShadowColor（用于定义二级阴影色)决定}
	DarkShadowColor = SFactor * (U_FixDarkShadow * ShadowColor + (1 - U_FixDarkShadow) * ShallowShadowColor) + (1 - SFactor) * DarkShadowColor;

	float rampS = smoothstep(0, U_ShadowSmooth, lambert - U_ShadowArea);
	float rampDS = smoothstep(0, U_DarkShadowSmooth, lambert - U_DarkShadowArea);
	
	ShallowShadowColor.rgb = mix(ShadowColor, BaseColor.rgb, rampS);
	DarkShadowColor.rgb = mix(DarkShadowColor.rgb, ShadowColor, rampDS);

	SFactor = floor(LightMapColor.g * V_BaseColor.r + 0.9f);
	vec4 FinalColor = vec4(1.0);
	FinalColor.rgb = SFactor * ShallowShadowColor + (1 - SFactor) * DarkShadowColor;

	
	//O_Color = vec4(DarkShadowColor,1.0);
	O_Color = vec4(rampShadowColor.rgb,1.0);
		
}

/*
				vec3 skinRamp = step(abs(LightMapColor.a * 255 - U_RampSkin.x), 10) * rampmaps[U_RampSkin.y]; // CoolShadowRamp2
				vec3 tightsRamp = step(abs(LightMapColor.a * 255 - U_RampTights.x), 10) * rampmaps[U_RampTights.y]; // CoolShadowRamp5
				vec3 softCommonRamp = step(abs(LightMapColor.a * 255 - U_RampSoft.x), 10) * rampmaps[U_RampSoft.y]; // CoolShadowRamp1
				vec3 hardSilkRamp = step(abs(LightMapColor.a * 255 - U_RampSilk.x), 10) * rampmaps[U_RampSilk.y]; // CoolShadowRamp3
				vec3 metalRamp = step(abs(LightMapColor.a * 255 - U_RampMetal.x), 10) * rampmaps[U_RampMetal.y]; // CoolShadowRamp4

				// 组合5个Ramp，得到最终的Ramp阴影，并根据rampValue与BaseColor结合。
				vec3 finalRamp = skinRamp + tightsRamp + metalRamp + softCommonRamp + hardSilkRamp;
				rampShadowColor = finalRamp;
*/