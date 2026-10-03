#version 450 core 
#pragma stage : vert
layout (location = 0) in vec3 aPos;

layout (location = 0) out vec3 WorldPos;

layout(set = 0, binding = 0) readonly buffer _unused_name_perframe
{
    uniform mat4 projection;
    uniform mat4 view;
}

void main()
{
    WorldPos = aPos;
    gl_Position =  projection * view * vec4(WorldPos, 1.0);
}

#version 450 core 
#pragma stage : frag

layout (location = 0) in vec3 WorldPos;
layout (location = 0) out vec4 FragColor;


layout(set = 0, binding = 0) uniform samplerCube environmentMap;
layout(push_constant) uniform float roughness;


void main()
{   

    //法线
    vec3 N = normalize(WorldPos);
    //反射方向    
    vec3 R = N;
    //假设观察方向救赎法线方向
    vec3 V = R;

    //我们使用蒙特卡洛方法对球面上进行采样，求得近似的积分结果，使用第低差分序列生成采样方向，totalWeight 在此处的作用类似于概率分布函数
    const uint SAMPLE_COUNT = 1024u;
    float totalWeight = 0.0;   
    //预计算结果
    vec3 prefilteredColor = vec3(0.0);     
    for(uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        //低差异序列向量生成,生成的低差异向量在切线空间
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        //将样本向量变换到世界空间并对场景的辐射度采样
        //使用重要性采样保证能更快收敛，从而以较低样本数得到比较好的结果
        vec3 H  = ImportanceSampleGGX(Xi, N, roughness);
        vec3 L  = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if(NdotL > 0.0)
        {
            // sample from the environment's mip level based on roughness/pdf
            float D   = DistributionGGX(N, H, roughness);
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float pdf = D * NdotH / (4.0 * HdotV) + 0.0001; 

            // 环境光 cubemap的分辨率
            float resolution = 512.0; 
            float saTexel  = 4.0 * PI / (6.0 * resolution * resolution);
            float saSample = 1.0 / (float(SAMPLE_COUNT) * pdf + 0.0001);

            float mipLevel = roughness == 0.0 ? 0.0 : 0.5 * log2(saSample / saTexel); 

            prefilteredColor += textureLod(environmentMap, L, mipLevel).rgb * NdotL;
            totalWeight      += NdotL;
        }
    }
    prefilteredColor = prefilteredColor / totalWeight;

    FragColor = vec4(prefilteredColor, 1.0);
}  