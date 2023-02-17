#type vertex

#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;

out vec2 TexCoords;
out vec3 WorldPos;
out vec3 Normal;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

void main()
{
    TexCoords = aTexCoords;
    WorldPos = vec3(model * vec4(aPos, 1.0));
    //法线没有位移运算，所以直接乘以mat3忽略齐次坐标的位移
    Normal = mat3(model) * aNormal;   

    gl_Position =  projection * view * vec4(WorldPos, 1.0);
}

#type fragment
#version 330 core

out vec4 FragColor;
in vec2 TexCoords;
in vec3 WorldPos;
in vec3 Normal;

const float PI = 3.14159265359;

//相机位置
uniform vec3 camPos;
//顶点材质
uniform sampler2D albedoMap;
uniform sampler2D normalMap;
uniform sampler2D armMap;


// lights
uniform vec3 lightPositions[4];
uniform vec3 lightColors[4];

//IBL 
uniform samplerCube irradianceMap;
uniform samplerCube prefilterMap;
uniform sampler2D brdfLUT;


//将贴图的切线空间发现转换到世界空间
vec3 getNormalFromMap()
{
    vec3 tangentNormal = texture(normalMap, TexCoords).xyz * 2.0 - 1.0;

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


//ggx法线分布函数,ggx的法线分布有较好的延尾性
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
//几何函数，为了满足反射光线能量守恒
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}
//使用数值方法拟合菲涅尔项
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
    

    //使用贴图加载材质属性
    vec3 albedo = pow(texture(albedoMap, TexCoords).rgb, vec3(2.2));
    float ao = texture(armMap, TexCoords).r;
    float roughness = texture(armMap, TexCoords).g;
    float metallic = texture(armMap, TexCoords).b;


    //使用微表面模型求解出brdf项，具体数学推理见computer graphics虎书相关章节，或者见rtr4相关章节
    vec3 N = getNormalFromMap();
    vec3 V = normalize(camPos - WorldPos);
    //使用反射光对预计算的 镜面反射进行采样
    vec3 R = reflect(-V,N);
    
    //对于非金属材质来说，FO基本保持在0.04这个值
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0,albedo,metallic);

    vec3 Lo = vec3(0.0);
    //因半球上积分并无数值解，若不采用蒙特卡洛方法或罗曼积分则无解决问题，此处使用hack直接假设光源到着色点只有一条光路能发生反射。则只需进行数值累加求和并进行归一化处理，就能得到积分的对应数值解
    for(int i = 0; i < 4; ++i) 
    {
        // 计算光源的radiance 光源在单位面积单位立体角上的辐照度
        vec3 L = normalize(lightPositions[i] - WorldPos);
        vec3 H = normalize(V + L);
        
        //计算radiance衰减
        float distance    = length(lightPositions[i] - WorldPos);
        float attenuation = 1.0 / (distance * distance);
        vec3  radiance     = lightColors[i] * attenuation;        

        // 微表面模型 brdf
        float NDF = DistributionGGX(N, H, roughness);        
        float G   = GeometrySmith(N, V, L, roughness);      
        vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);       

        vec3 nominator    = NDF * G * F;
        //计算归一化项
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001; 
        vec3 specular     = nominator / denominator;

        //反射系数
        vec3 kS = F;
        //漫反射系数
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;     

    
        //使用ndotl来表示微表面的受光面积
        float NdotL = max(dot(N, L), 0.0);                
        // 黎曼积分
        Lo += (kD * albedo / PI + specular) * radiance * NdotL; 
    }   

    //补足环境光的菲涅尔项，我们在预计算的时候并未考虑F项，因为F可以比较简便的实时运算得到
    vec3 F = fresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
    //环境光
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - metallic;	  
    
    
    vec3 irradiance = texture(irradianceMap, N).rgb;
    vec3 diffuse    = irradiance * albedo;
    
    //使用预计算结果得到IBL的镜面反射部分
    //我们预先计算了5个级别的mipmap，防止对不存在mipmap采样
    const float MAX_REFLECTION_LOD = 4.0;
    //粗糙度越高，则在更高的层级采样，采样结果更加的模糊（glosse）
    vec3 prefilteredColor = textureLod(prefilterMap, R,  roughness * MAX_REFLECTION_LOD).rgb;
    vec2 brdf  = texture(brdfLUT, vec2(max(dot(N, V), 0.0), roughness)).rg;
    vec3 specular = prefilteredColor * (F * brdf.x + brdf.y);

    vec3 ambient = (kD * diffuse + specular) * ao; 
    vec3 color   = ambient + Lo;
    //我们的计算是在线性空间，需要进行伽玛矫正，防止超过1的值被截断
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));  
    
    FragColor = vec4(color, 1.0);

}