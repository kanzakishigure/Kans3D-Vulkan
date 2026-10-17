#pragma once
#include <glm/glm.hpp>
namespace Kans
{
    struct DirLight
    {
        glm::vec3 Dirction {0};
        glm::vec3 Diffuse_Intensity {0};
        glm::vec3 Specular_Intensity {0};
        glm::vec3 Ambient_Intensity {0};
    };
    struct PointLight
    {
        glm::vec3 Position {0};
        glm::vec3 Diffuse_Intensity {0};
        glm::vec3 Specular_Intensity {0};
        glm::vec3 Ambient_Intensity {0};
    };

    struct PipLineState
    {
        bool EnableOutline       = false;
        bool EnableToneShader    = false;
        bool EnableStencil       = false;
        bool EnableDebugNormal   = false;
        bool EnableDefaultShader = true;
        bool EnableGridShader    = true;
    };

    struct RenderResource
    {
        PointLight   pointLight;
        DirLight     dirLight;
        PipLineState Piplinestate;
    };

} // namespace Kans
