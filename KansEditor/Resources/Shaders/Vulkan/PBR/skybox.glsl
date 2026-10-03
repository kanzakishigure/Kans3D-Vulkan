#version 450 core 
#pragma stage : vert
layout (location = 0) in vec3 aPos;
layout (location = 0) out vec3 localPos;



layout(set = 0, binding = 0) readonly buffer _unused_name_perframe
{
    mat4 U_Projection;
    mat4 U_View;
};

void main()
{
    localPos = aPos;

    //忽略平移运算
    mat4 rotView = mat4(mat3(U_View)); 
    vec4 clipPos = U_Projection * rotView * vec4(localPos, 1.0);

    gl_Position = clipPos.xyww;
}

#version 450 core 
#pragma stage : frag

layout (location = 0) in vec3 localPos;

layout (location = 0) out vec4 FragColor;

//立方体贴图

layout (set = 0,binding = 0) uniform samplerCube environmentMap;

void main()
{
    vec3 envColor = texture(environmentMap, localPos).rgb;

    //伽玛矫正
    envColor = envColor / (envColor + vec3(1.0));
    envColor = pow(envColor, vec3(1.0/2.2)); 

    FragColor = vec4(envColor, 1.0);
}