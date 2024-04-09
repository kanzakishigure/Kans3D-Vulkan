#type vertex
#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 WorldPos;

uniform mat4 projection;
uniform mat4 view;

void main()
{
    WorldPos = aPos;
    gl_Position =  projection * view * vec4(WorldPos, 1.0);
}















#type fragment
#version 330 core
out vec4 FragColor;
in vec3 WorldPos;

uniform samplerCube environmentMap;

const float PI = 3.14159265359;

void main()
{		


    //使用卷积与预计算每一个shaderpoint的irradiance
    //因为半球上对环境光的疾风并无解析解，使用黎曼积分的方式拆解为二重积分，并求出积分
    //同时，通过熟知图像处理的应该知道，一个函数和另一个函数在一个平滑积分限上的积分，
    //其实就是求一个冲击函数对另一个函数的卷积,所以我们得出的图像看起来比较像是一个模糊过的天空盒
    vec3 N = normalize(WorldPos);
    vec3 irradiance = vec3(0.0);  

    //用于将切线空间坐标转换到笛卡尔坐标
    vec3 up    = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));

    //积分步长
    float sampleDelta = 0.025;
    //为了能力守恒，我们的积分需要有常数归一化项
    float nrSamples = 0.0; 
    //单位立体角上的积分dwi可以拆解为 sin(θ)dθ dϕ的二重积分
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