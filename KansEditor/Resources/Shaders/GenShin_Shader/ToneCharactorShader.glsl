
#type vertex
#version 330 core


layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TextureCroods;
layout(location = 3) in vec4 a_BaseColor;
layout(location = 4) in vec4 a_Tangent;

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

uniform float U_RampShadowRange; 
uniform float U_RampAOShadowRange; 

uniform float U_ShadowIntensity; 
uniform float U_BrightIntensity;
uniform float U_AOIntensity;
uniform float U_CharacterIntensity;
uniform float U_RampIntensity;

uniform float U_SpecularThreshold_1;
uniform float U_SpecularIntensity_1;
uniform float U_SpecularThreshold_2;
uniform float U_SpecularIntensity_2;
uniform float U_SpecularThreshold_3;
uniform float U_SpecularIntensity_3;


uniform bool _UseRamp; 
uniform bool _UseFaceMask;


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


	vec4 BaseColor = texture2D(material.U_DiffuseTexture,vec2(V_TexCroods.x,V_TexCroods.y));
	
    vec4 LightMapColor = texture2D(material.U_ToneLightMap,vec2(V_TexCroods.x,1.0-V_TexCroods.y));
	//Prep
	//lambert(LoN)
	float lambert =  max(0.0,dot(lightdir,norm));
	lambert = lambert*0.5+0.5;
	
	float NoV = dot(viewDir,norm);

	vec3 finalColor = vec3(1.0);
	
	//使用ramp贴图
	if(_UseRamp)
	{
		
		
		//从lightMap中读取到对应材质所在区间
		float rampV = LightMapColor.a;
		float ShadowAOMask = LightMapColor.g;
		ShadowAOMask = smoothstep(0.2, 0.6,clamp(ShadowAOMask*2.0,0.0,1.0));
		ShadowAOMask = mix(0.5,1.0,ShadowAOMask);
		float rampU = lambert*ShadowAOMask*(1.0/U_RampShadowRange - 0.03);
		rampU = clamp(rampU,0.0,0.98);
		
		
	
		
		//tights
		vec3 rampShadowColor1 = pow(texture2D(material.U_ToneRampMap,vec2(rampU,0.05)).rgb,vec3(1.0/2.2));
		// hard/emission/specular/silk
		vec3 rampShadowColor2 = pow(texture2D(material.U_ToneRampMap,vec2(rampU,0.15)).rgb,vec3(1.0/2.2));
		//metal
		vec3 rampShadowColor3 = pow(texture2D(material.U_ToneRampMap,vec2(rampU,0.25)).rgb,vec3(1.0/2.2));
		//skin
		vec3 rampShadowColor4 = pow(texture2D(material.U_ToneRampMap,vec2(rampU,0.35)).rgb,vec3(1.0/2.2));
		//soft/common
		vec3 rampShadowColor5 = pow(texture2D(material.U_ToneRampMap,vec2(rampU,0.45)).rgb,vec3(1.0/2.2));
		
		vec3 rampShadowColorCool1 = texture2D(material.U_ToneRampMap,vec2(rampU,0.55)).rgb;
		vec3 rampShadowColorCool2 = texture2D(material.U_ToneRampMap,vec2(rampU,0.65)).rgb;
		vec3 rampShadowColorCool3 = texture2D(material.U_ToneRampMap,vec2(rampU,0.75)).rgb;
		vec3 rampShadowColorCool4 = texture2D(material.U_ToneRampMap,vec2(rampU,0.85)).rgb;
		vec3 rampShadowColorCool5 = texture2D(material.U_ToneRampMap,vec2(rampU,0.95)).rgb;

		


		vec3 tightsRamp = step(abs(LightMapColor.a - 0.7),     0.05) * rampShadowColor5;          
		vec3 skinRamp = step(abs(LightMapColor.a - 1.0),     0.05) * rampShadowColor4;  
		vec3 softCommonRamp = step(abs(LightMapColor.a  - 0.5), 0.05 ) * rampShadowColor3;            
		vec3 hardSilkRamp = step(abs(LightMapColor.a  - 0.3),   0.1) * rampShadowColor2;            
		vec3 metalRamp = step(abs(LightMapColor.a - 0.0),       0.05) * rampShadowColor1;    
		
		vec3 rampColor = skinRamp+tightsRamp+softCommonRamp +hardSilkRamp+metalRamp;
		rampColor =	pow(rampColor,vec3(2.2));
		/*
		float rampValue =  step(U_RampShadowRange,lambert);
		vec3 rampShadowColor = mix(BaseColor.rgb*rampColor,BaseColor.rgb,rampValue);//lambert rampShadowColor
		*/
		
		vec3 BaseMapShadowed = mix(BaseColor.rgb * rampColor, BaseColor.rgb,ShadowAOMask); //AO rampShadowColor
		BaseMapShadowed = mix(BaseColor.rgb,BaseMapShadowed,U_AOIntensity); //控制AO的强度

		float isLightSide = ShadowAOMask*step(U_RampShadowRange,lambert);//AO区域保持常暗
		vec3 shadowWithAO =  mix(BaseMapShadowed, BaseColor.rgb * rampColor, U_RampAOShadowRange) * U_ShadowIntensity;		
		vec3 Diffuse = mix(shadowWithAO ,U_BrightIntensity * BaseMapShadowed ,isLightSide*U_RampIntensity) * U_CharacterIntensity * pointLight.Diffuse_Intensity;

		//specular
		int specularType = int(LightMapColor.r*255);
		
		vec3 specularColor = vec3(0.0);
		vec3 StepSpecular = vec3(0.0);
		float specularMask = LightMapColor.b;
		//丝绸
		if(specularType<10)
		{
			StepSpecular = clamp(NoV,0.0,1.0)*U_SpecularIntensity_1*vec3(specularMask); //可做修改* SpecularIntensityMask
            StepSpecular *= BaseColor.rgb;           
		}

		//黑丝
		if(specularType>15&&specularType<35)
		{
			StepSpecular = clamp(NoV,0.0,1.0) *U_SpecularIntensity_2*vec3(specularMask);; //可做修改* SpecularIntensityMask
            StepSpecular *= BaseColor.rgb;  
		}
		//绳子
		if(specularType>125&&specularType<135)
		{
			StepSpecular = clamp(NoV,0.0,1.0) *U_SpecularIntensity_3*vec3(specularMask);; //可做修改* SpecularIntensityMask
            StepSpecular *= BaseColor.rgb;  
		}
		//hair
		if(specularType>145&&specularType<165)
		{
			
		}
		//金属
		if(specularType>250&&specularType<260)
		{
			
		}


		finalColor =vec3(StepSpecular) + Diffuse;
		
	}

	O_Color = vec4(finalColor.rgb,1.0);
	
	
		
}
