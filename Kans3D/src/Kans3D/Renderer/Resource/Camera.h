#pragma once
#include <glm/glm.hpp>
#include "AntiAliasing.h"
#include "BloomSettings.h"

namespace Kans
{

    class Camera
    {
    public:
        Camera() = default;
        Camera(const glm::mat4& ProjectionMatrix);
        virtual ~Camera() = default;

        const glm::mat4& GetProjectionMatrix() const { return m_ProjectionMatrix; }
        void SetProjectionMatirx(const glm::mat4& ProjectionMatrix) { m_ProjectionMatrix = ProjectionMatrix; }
        void SetPerspectiveProjectionMatrix(const float radFov,
                                            const float width,
                                            const float height,
                                            const float nearP,
                                            const float farP);

        float  GetExposure() const { return m_Exposure; }
        float& GetExposure() { return m_Exposure; }
        void   SetExposure(float exposure) { m_Exposure = exposure; }

        const BloomSettings& GetBloomSettings() const { return m_Bloom; }
        BloomSettings&       GetBloomSettings() { return m_Bloom; }
        void                 SetBloomSettings(const BloomSettings& settings) { m_Bloom = settings.Validated(); }

        AntiAliasing GetAntiAliasing() const { return m_AntiAliasing; }
        void         SetAntiAliasing(AntiAliasing mode)
        {
            AntiAliasingSamples(mode);
            m_AntiAliasing = mode;
        }

    protected:
        glm::mat4 m_ProjectionMatrix;
        float     m_Exposure = 0.8f;
        BloomSettings m_Bloom;
        AntiAliasing  m_AntiAliasing = AntiAliasing::None;
    };

} // namespace Kans