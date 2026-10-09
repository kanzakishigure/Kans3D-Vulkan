#include "kspch.h"
#include "MeshFactory.h"
#include "RenderAssetCache.h"

namespace Kans
{
    MeshData MeshFactory::CreateCubeData(const glm::vec3& size)
    {
        const glm::vec3 h = size * 0.5f;
        MeshData        mesh;
        mesh.Vertices.resize(8);
        const glm::vec3 corners[8] = {
            {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}};
        for (size_t i = 0; i < 8; ++i)
        {
            const glm::vec3 position  = corners[i] * h;
            const glm::vec3 normal    = glm::normalize(corners[i]);
            mesh.Vertices[i].Position = {position.x, position.y, position.z};
            mesh.Vertices[i].Normal   = {normal.x, normal.y, normal.z};
        }
        mesh.Indices = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1, 7, 6, 5, 5, 4, 7,
                        4, 0, 3, 3, 7, 4, 4, 5, 1, 1, 0, 4, 3, 2, 6, 6, 7, 3};
        return mesh;
    }

    Ref<VertexArray> MeshFactory::CreateCube(const glm::vec3& size)
    {
        auto uploaded = UploadMesh(CreateCubeData(size));
        CORE_ASSERT(std::holds_alternative<GpuMesh>(uploaded), "Cannot upload the built-in cube");
        return std::get<GpuMesh>(std::move(uploaded)).VertexArray;
    }

    Ref<VertexArray> MeshFactory::GetCube()
    {
        static Ref<VertexArray> cube = CreateCube(glm::vec3(2.0f));
        return cube;
    }

    Ref<VertexArray> MeshFactory::CreatQuad(const glm::vec2 size)
    {
        struct QuadVertex
        {
            glm::vec3 Position;
            glm::vec2 TextureCroods;
        };
        const QuadVertex vertices[4] = {{{-size.x / 2.0f, 0.0f, -size.y / 2.0f}, {0.0f, 0.0f}},
                                        {{size.x / 2.0f, 0.0f, -size.y / 2.0f}, {1.0f, 0.0f}},
                                        {{size.x / 2.0f, 0.0f, size.y / 2.0f}, {1.0f, 1.0f}},
                                        {{-size.x / 2.0f, 0.0f, size.y / 2.0f}, {0.0f, 1.0f}}};
        uint32_t         indices[6]  = {0, 1, 2, 2, 3, 0};

        Ref<VertexBuffer> vertexBuffer = VertexBuffer::Create((float*)vertices, uint32_t(sizeof(vertices)));
        vertexBuffer->SetBufferLayout({
            {ShaderDataType::Float3, "a_Position"},
            {ShaderDataType::Float2, "a_TextureCroods"},
        });
        Ref<VertexArray> array = VertexArray::Create();
        array->AddVertexBuffer(vertexBuffer);
        array->SetIndexBuffer(IndexBuffer::Create(indices, 6));
        array->Unbind();
        return array;
    }
} // namespace Kans
