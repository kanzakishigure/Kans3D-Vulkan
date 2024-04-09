#version 450 core 
#pragma stag : vert

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TextureCroods;
layout(location = 3) in vec4 a_BaseColor;
layout(location = 4) in vec3 a_Tangent;

layout(location = 0) out vec2 TexCoords;
layout(location = 1) out vec3 WorldPos;
layout(location = 2) out vec3 Normal;



layout(set = 0, binding = 0) readonly buffer _unused_name_perframe
{
    uniform mat4 U_ViewProjection;
    uniform mat4 U_Transform;
}


void main()
{
    TexCoords = a_TextureCroods;
    WorldPos = vec3(U_Transform * vec4(a_Position, 1.0));
    //法线没有位移运算，所以直接乘�??mat3忽略齐�?�坐标的位移
    Normal = mat3(U_Transform) * a_Normal;
    gl_Position =  U_ViewProjection* vec4(WorldPos, 1.0);
}











#version 450 core 
#pragma stag : frag

#include<Env.h>

layout(location = 0) out vec4 FragColor;

layout(location = 0) in vec2 TexCoords;
layout(location = 1) in vec3 WorldPos;
layout(location = 2) in vec3 Normal;

const float PI = 3.14159265359;

struct Material
{

    //PBR
layout(set = 0, binding = 0) sampler2D U_AlbedoTexture;
layout(set = 0, binding = 1) sampler2D U_AOTexture;
layout(set = 0, binding = 2) sampler2D U_RoughTexture;
layout(set = 0, binding = 3) sampler2D U_MetalTexture;
layout(set = 0, binding = 4) sampler2D U_NormalTexture;
layout(set = 0, binding = 5) sampler2D U_EmissionTexture;
};


layout(set = 0, binding = 0) readonly buffer _unused_name_perframe
{
    //相机位置
    uniform vec3 U_ViewPos;
    // lights
    uniform vec3 lightPositions[4];
    uniform vec3 lightColors[4];
    //shading point Material
    uniform Material material;
};



//将贴图的切线空间发现�??换到世界空间
vec3 getNormalFromMap()
{
    vec3 tangentNormal = texture(material.U_NormalTexture, TexCoords).xyz * 2.0 - 1.0;

    vec3 Q1  = dFdx(WorldPos);
    vec3 Q2  = dFdy(WorldPos);
    vec2 st1 = dFdx(TexCoords);
    vec2 st2 = dFdy(TexCoords);

    vec3 N   = normalize(Normal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}


//ggx法线分布函数,ggx的法线分布有较好的延尾�?
float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}


float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
//几何函数，为了满足反射光线能量守�??
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
//使用数值方法拟合菲涅尔�??
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}   
void main()
{
    

    //使用贴图加载材质属�?
    vec3 albedo = pow(texture(material.U_AlbedoTexture, TexCoords).rgb, vec3(2.2));
    float ao = texture(material.U_AOTexture, TexCoords).r;
    float roughness = texture(material.U_RoughTexture, TexCoords).g;
    float metallic = texture(material.U_MetalTexture, TexCoords).b;


    //使用�??表面模型求解出brdf项，具体数�?�推理�?�computer graphics虎书相关章节，或者�?�rtr4相关章节
    vec3 N = getNormalFromMap();
    vec3 V = normalize(U_ViewPos - WorldPos);
    //使用反射光�?��?��?�算�?? 镜面反射进�?�采�??
    vec3 R = reflect(-V,N);
    
    //对于非金属材质来说，FO基本保持�??0.04这个�??
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0,albedo,metallic);

    vec3 Lo = vec3(0.0);
    //因半球上�??分并无数值解，若不采用蒙特卡洛方法或罗曼�??分则无解决问题，此�?�使用hack直接假�?�光源到着色点�??有一条光�??能发生反射。则�??需进�?�数值累加求和并进�?�归一化�?�理，就能得到积分的对应数值解
    for(int i = 0; i < 4; ++i) 
    {
        // 计算光源的radiance 光源在单位面�??单位立体角上的辐照度
        vec3 L = normalize(lightPositions[i] - WorldPos);
        vec3 H = normalize(V + L);
        
        //计算radiance衰减
        float distance    = length(lightPositions[i] - WorldPos);
        float attenuation = 1.0 / (distance * distance);
        vec3  radiance     = lightColors[i] * attenuation;        

        // �??表面模型 brdf
        float NDF = DistributionGGX(N, H, roughness);        
        float G   = GeometrySmith(N, V, L, roughness);      
        vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);       

        vec3 nominator    = NDF * G * F;
        //计算归一化项
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001; 
        vec3 specular     = nominator / denominator;

        //反射系数
        vec3 kS = F;
        //�??反射系数
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;     

    
        //使用ndotl来表示微表面的受光面�??
        float NdotL = max(dot(N, L), 0.0);                
        // 黎曼�??�??
        Lo += (kD * albedo / PI + specular) * radiance * NdotL; 
    }   

    //补足�??境光的菲涅尔项，我们�?�??��?�算的时候并�??考虑F项，因为F�??以比较简便的实时运算得到
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    //�??境光
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;	  
    
    
    vec3 irradiance = texture(U_IrradianceMap, N).rgb;
    vec3 diffuse    = irradiance * albedo;
    
    //使用预�?�算结果得到IBL的镜面反射部�??
    //我们预先计算�??5�??级别的mipmap，防�?�??�不存在mipmap采样
    const float MAX_REFLECTION_LOD = 4.0;
    //粗糙度越高，则在更高的层级采样，采样结果更加的模糊（glosse�??
    vec3 prefilteredColor = textureLod(U_PrefilterMap, R,  roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf  = texture(U_BrdfLUTMap, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 ambient = (kD * diffuse + specular) * ao; 
    vec3 color   = ambient + Lo;
    //我们的�?�算�??在线性空间，需要进行伽玛矫正，防�?�超�??1的值�??�??�??
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));  
    
    FragColor = vec4(color, 1.0);

}