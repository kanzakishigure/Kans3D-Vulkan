#version 450 core 
#pragma stag : vert
layout (location = 0) in  vec3 aPos;

layout (location = 0) out vec3 WorldPos;

uniform mat4 projection;
uniform mat4 view;

void main()
{
    WorldPos = aPos;
    gl_Position =  projection * view * vec4(WorldPos, 1.0);
}





#version 450 core 
#pragma stag : frag

layout (location = 0) in vec3 WorldPos;

layout (location = 0) out vec4 FragColor;

layout(set = 0, binding = 0) uniform samplerCube environmentMap;

const float PI = 3.14159265359;

void main()
{		


    //浣跨敤鍗风Н涓庨?勮?＄畻姣忎竴涓猻haderpoint鐨刬rradiance
    //鍥犱负鍗婄悆涓婂?圭幆澧冨厜鐨勭柧椋庡苟鏃犺В鏋愯В锛屼娇鐢ㄩ粠鏇肩Н鍒嗙殑鏂瑰紡鎷嗚В涓轰簩閲嶇Н鍒嗭紝骞舵眰鍑虹Н鍒?
    //鍚屾椂锛岄€氳繃鐔熺煡鍥惧儚澶勭悊鐨勫簲璇ョ煡閬擄紝涓€涓?鍑芥暟鍜屽彟涓€涓?鍑芥暟鍦ㄤ竴涓?骞虫粦绉?鍒嗛檺涓婄殑绉?鍒嗭紝
    //鍏跺疄灏辨槸姹備竴涓?鍐插嚮鍑芥暟瀵瑰彟涓€涓?鍑芥暟鐨勫嵎绉?,鎵€浠ユ垜浠?寰楀嚭鐨勫浘鍍忕湅璧锋潵姣旇緝鍍忔槸涓€涓?妯＄硦杩囩殑澶╃┖鐩?
    vec3 N = normalize(WorldPos);
    vec3 irradiance = vec3(0.0);  

    //鐢ㄤ簬灏嗗垏绾跨┖闂村潗鏍囪浆鎹㈠埌绗涘崱灏斿潗鏍?
    vec3 up    = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    //绉?鍒嗘?ラ暱
    float sampleDelta = 0.025;
    //涓轰簡鑳藉姏瀹堟亽锛屾垜浠?鐨勭Н鍒嗛渶瑕佹湁甯告暟褰掍竴鍖栭」
    float nrSamples = 0.0; 
    //鍗曚綅绔嬩綋瑙掍笂鐨勭Н鍒哾wi鍙?浠ユ媶瑙ｄ负 sin(胃)d胃 d蠒鐨勪簩閲嶇Н鍒?
    for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // spherical to cartesian (in tangent space)
            vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
            // tangent space to world
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N; 

            irradiance += texture(environmentMap, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = PI * irradiance * (1.0 / float(nrSamples));
    
    FragColor = vec4(irradiance, 1.0);
    }