#version 450 core 
#pragma stag : vert
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
#pragma stag : frag

layout (location = 0) in  vec3 WorldPos;

layout (location = 0) out vec4 FragColor;

layout(set = 0, binding = 0) uniform sampler2D equirectangularMap;

const vec2 invAtan = vec2(0.1591, 0.3183);
vec2 SampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= invAtan;
    uv += 0.5;
    return uv;
}

void main()
{		
    vec2 uv = SampleSphericalMap(normalize(WorldPos));
    vec3 color = texture(equirectangularMap, uv).rgb;
    
    FragColor = vec4(color, 1.0);
    
}