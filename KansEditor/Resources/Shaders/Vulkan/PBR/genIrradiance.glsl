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


    //使用卷积与�?��?�算每一个shaderpoint的irradiance
    //因为半球上�?�环境光的疾风并无解析解，使用黎曼积分的方式拆解为二重积分，并求出积�?
    //同时，通过熟知图像处理的应该知道，一�?函数和另一�?函数在一�?平滑�?分限上的�?分，
    //其实就是求一�?冲击函数对另一�?函数的卷�?,所以我�?得出的图像看起来比较像是一�?模糊过的天空�?
    vec3 N = normalize(WorldPos);
    vec3 irradiance = vec3(0.0);  

    //用于将切线空间坐标转换到笛卡尔坐�?
    vec3 up    = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    //�?分�?�长
    float sampleDelta = 0.025;
    //为了能力守恒，我�?的积分需要有常数归一化项
    float nrSamples = 0.0; 
    //单位立体角上的积分dwi�?以拆解为 sin(θ)dθ dϕ的二重积�?
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