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
    //鍋囪?捐?傚療鏂瑰悜鏁戣祹娉曠嚎鏂瑰悜
    vec3 V = R;

    //鎴戜滑浣跨敤钂欑壒鍗℃礇鏂规硶瀵圭悆闈?涓婅繘琛岄噰鏍凤紝姹傚緱杩戜技鐨勭Н鍒嗙粨鏋滐紝浣跨敤绗?浣庡樊鍒嗗簭鍒楃敓鎴愰噰鏍锋柟鍚戯紝totalWeight 鍦ㄦ?ゅ?勭殑浣滅敤绫讳技浜庢?傜巼鍒嗗竷鍑芥暟
    const uint SAMPLE_COUNT = 1024u;
    float totalWeight = 0.0;   
    //棰勮?＄畻缁撴灉
    vec3 prefilteredColor = vec3(0.0);     
    for(uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        //浣庡樊寮傚簭鍒楀悜閲忕敓鎴?,鐢熸垚鐨勪綆宸?寮傚悜閲忓湪鍒囩嚎绌洪棿
        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        //灏嗘牱鏈?鍚戦噺鍙樻崲鍒颁笘鐣岀┖闂村苟瀵瑰満鏅?鐨勮緪灏勫害閲囨牱
        //浣跨敤閲嶈?佹€ч噰鏍蜂繚璇佽兘鏇村揩鏀舵暃锛屼粠鑰屼互杈冧綆鏍锋湰鏁板緱鍒版瘮杈冨ソ鐨勭粨鏋?
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

            // 鐜?澧冨厜 cubemap鐨勫垎杈ㄧ巼
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