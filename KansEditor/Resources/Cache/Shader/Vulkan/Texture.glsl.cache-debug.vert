#  
  A                 GLSL.std.450                      main                !   %   '   .   2   8        Resources/Shaders/debug/Texture.glsl     i   �     #version 450 core
#extension GL_GOOGLE_include_directive : enable
#line 1 "Resources/Shaders/debug/Texture.glsl"

#pragma stag:vert

#line 1 "Resources/Shaders/Include/env.glslh"


float RadicalInverse_VdC(uint bits)
{
     bits =(bits << 16u)|(bits >> 16u);
     bits =((bits & 0x55555555u)<< 1u)|((bits & 0xAAAAAAAAu)>> 1u);
     bits =((bits & 0x33333333u)<< 2u)|((bits & 0xCCCCCCCCu)>> 2u);
     bits =((bits & 0x0F0F0F0Fu)<< 4u)|((bits & 0xF0F0F0F0u)>> 4u);
     bits =((bits & 0x00FF00FFu)<< 8u)|((bits & 0xFF00FF00u)>> 8u);
     return float(bits)* 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint N)
{
 return vec2(float(i)/ float(N), RadicalInverse_VdC(i));
}

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
 float a = roughness * roughness;

 float phi = 2.0 * 3.1415926535 * Xi . x;
 float cosTheta = sqrt((1.0 - Xi . y)/(1.0 +(a * a - 1.0)* Xi . y));
 float sinTheta = sqrt(1.0 - cosTheta * cosTheta);


 vec3 H;
 H . x = cos(phi)* sinTheta;
 H . y = sin(phi)* sinTheta;
 H . z = cosTheta;


 vec3 up = abs(N . z)< 0.999 ? vec3(0.0, 0.0, 1.0): vec3(1.0, 0.0, 0.0);
 vec3 tangent = normalize(cross(up, N));
 vec3 bitangent = cross(N, tangent);

 vec3 sampleVec = tangent * H . x + bitangent * H . y + N * H . z;
 return normalize(sampleVec);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{

    float a = roughness;
    float k =(a * a)/ 2.0;

    float nom = NdotV;
    float deom = NdotV *(1.0 - k)+ k;

    return nom / deom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec2 IntegrateBRDF(float NdotV, float roughness)
{
    vec3 V;
    V . x = sqrt(1.0 - NdotV * NdotV);
    V . y = 0.0;
    V . z = NdotV;

    float A = 0.0;
    float B = 0.0;

    vec3 N = vec3(0.0, 0.0, 1.0);

    const uint SAMPLE_COUNT = 1024u;
    for(uint i = 0u;i < SAMPLE_COUNT;++ i)
    {


        vec2 Xi = Hammersley(i, SAMPLE_COUNT);
        vec3 H = ImportanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H)* H - V);

        float NdotL = max(L . z, 0.0);
        float NdotH = max(H . z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if(NdotL > 0.0)
        {
            float G = GeometrySmith(N, V, L, roughness);
            float G_Vis =(G * VdotH)/(NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);

            A +=(1.0 - Fc)* G_Vis;
            B += Fc * G_Vis;
        }
    }
    A /= float(SAMPLE_COUNT);
    B /= float(SAMPLE_COUNT);
    return vec2(A, B);
}
#line 5 "Resources/Shaders/debug/Texture.glsl"

layout(location = 0)in vec3 a_Position;
layout(location = 1)in vec4 a_Color;
layout(location = 2)in vec2 a_TexCoord;
layout(location = 3)in float a_TexIndex;
layout(location = 4)in float a_TilingFactor;
layout(location = 5)in int a_EntityID;

layout(std140, binding = 0)uniform Camera
{
 mat4 u_ViewProjection;
};

struct VertexOutput
{
 vec4 Color;
 vec2 TexCoord;
 float TexIndex;
 float TilingFactor;
};

layout(location = 0)out VertexOutput Output;
layout(location = 4)out flat int v_EntityID;

void main()
{
 Output . Color = a_Color;
 Output . TexCoord = a_TexCoord;
 Output . TexIndex = a_TexIndex;
 Output . TilingFactor = a_TilingFactor;
 v_EntityID = a_EntityID;

 gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
}
     
 GL_GOOGLE_cpp_style_line_directive    GL_GOOGLE_include_directive      main      
   VertexOutput      
       Color     
      TexCoord      
      TexIndex      
      TilingFactor         Output       a_Color      a_TexCoord       a_TexIndex    !   a_TilingFactor    %   v_EntityID    '   a_EntityID    ,   gl_PerVertex      ,       gl_Position   ,      gl_PointSize      ,      gl_ClipDistance   ,      gl_CullDistance   .         0   Camera    0       u_ViewProjection      2         8   a_Position  J entry-point main    J client vulkan100    J target-env spirv1.5 J target-env vulkan1.2    J entry-point main    G            G           G           G           G  !         G  %      G  %         G  '         H  ,              H  ,            H  ,            H  ,            G  ,      H  0          H  0       #       H  0             G  0      G  2   "       G  2   !       G  8               !                              	           
      	                  
   ;                       +                        ;                       +                    	   ;                    	   +                       ;                       +            ;     !         $         ;  $   %         &         ;  &   '        )           +  )   *        +      *     ,         +   +      -      ,   ;  -   .        /           0   /      1      0   ;  1   2         3      /     6            7      6   ;  7   8      +     :     �?           6               �                 =           A              >                     =  	         A              >             !       =           A              >             "       =     "   !   A     #          >  #   "        #       =     (   '   >  %   (        %       A  3   4   2      =  /   5   4   =  6   9   8   Q     ;   9       Q     <   9      Q     =   9      P     >   ;   <   =   :   �     ?   5   >   A     @   .      >  @   ?   �  8  