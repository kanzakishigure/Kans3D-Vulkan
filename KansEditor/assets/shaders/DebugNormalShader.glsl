#type vertex
#version 330 core
layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 3) in vec4 a_BaseColor;
layout(location = 3) in vec3 a_Tangent;

uniform mat4 U_ViewProjection;
uniform mat4 U_Transform;



out VS_OUT {
    vec3 normal;
} vs_out;
void main()
{


  //（Moldle-1）T
	vec3 BakedNormal =  mat3(U_ViewProjection)*mat3(U_ViewProjection)*mat3(transpose(inverse(U_Transform)))*a_Normal;
    BakedNormal = normalize(BakedNormal);
	vec3 tangent = mat3(U_ViewProjection)*mat3(U_Transform)*a_Tangent;
    tangent = normalize(tangent);
	vec3 bitangent =  cross(BakedNormal,tangent);
    bitangent = normalize(bitangent);
	mat3 tangentTransform = mat3(tangent, bitangent, BakedNormal);
	BakedNormal = tangentTransform*a_BaseColor.rgb;
    BakedNormal = normalize(BakedNormal);

    // we will shreek the normal in the screen space so trans the normal to the sceenspace;
    vec3 Normal =  mat3(U_ViewProjection)*mat3(transpose(inverse(U_Transform)))*a_Normal;
	// gl_Position is in the screen space
    vec4 o_Position = U_ViewProjection*U_Transform*vec4(a_Position, 1.0);
    gl_Position = o_Position;
    Normal = normalize(Normal);

    //vertex Normal in sceenspace
    vs_out.normal = Normal.xyz;
    //baked Normal in sceenspace
    //vs_out.normal = BakedNormal.xyz;
    
}

#type geometry
#version 330 core
layout (triangles) in;
layout (line_strip, max_vertices = 6) out;


in VS_OUT {
    vec3 normal;
} gs_in[];
const float MAGNITUDE = 0.05f;


void GenerateLine(int index)
{
    gl_Position = gl_in[index].gl_Position;
    EmitVertex();
    gl_Position = gl_in[index].gl_Position + vec4(gs_in[index].normal, 0.0) * MAGNITUDE;
    EmitVertex();
    EndPrimitive();
}
void main() {    
    GenerateLine(0); // 第一个顶点法线
    GenerateLine(1); // 第二个顶点法线
    GenerateLine(2); // 第三个顶点法线
}


#type fragment
#version 330 core
layout(location = 0) out vec4 O_Color;

void main()
{
    O_Color = vec4(1.0);
}
