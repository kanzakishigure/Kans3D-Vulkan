#include <gtest/gtest.h>
#include "Kans3D/Renderer/Resource/InternalShader.h"

namespace
{
    using namespace Kans;
    const std::string Vertex   = R"(#version 450
layout(set=0,binding=2,std140) uniform Draw { mat4 MVP; } p;
layout(location=0) in vec3 position;
void main() { gl_Position = p.MVP * vec4(position, 1); }
)";
    const std::string Fragment = R"(#version 450
layout(set=0,binding=7) uniform sampler2D image;
layout(location=0) out vec4 color;
void main() { color = texture(image, vec2(0.5)); }
)";

    TEST(InternalShader, ProducesValidatedReflectedSpirvAndReusesCpuProduct)
    {
        // Catches a shortcut that returns unreflected code or drops explicit descriptor slots.
        const auto& result = GetInternalShaderData("ReflectionTest", Vertex, Fragment);
        ASSERT_TRUE(std::holds_alternative<ShaderData>(result)) << std::get<AssetError>(result).Message;
        const auto& data = std::get<ShaderData>(result);
        ASSERT_EQ(data.Code.Modules.size(), 2u);
        ASSERT_EQ(data.Reflection.size(), 2u);
        EXPECT_EQ(data.Code.Modules[0].Words.front(), 0x07230203u);
        const auto reflected = InspectShaderProgram(data.Code);
        ASSERT_TRUE(std::holds_alternative<std::vector<ShaderStageReflection>>(reflected));
        const auto& stages = std::get<std::vector<ShaderStageReflection>>(reflected);
        ASSERT_EQ(stages[1].Bindings.size(), 1u);
        EXPECT_EQ(stages[1].Bindings[0].Set, 0u);
        EXPECT_EQ(stages[1].Bindings[0].Binding, 7u);
        EXPECT_EQ(stages[1].Bindings[0].Kind, ShaderBindingKind::CombinedImageSampler);
        EXPECT_EQ(&result, &GetInternalShaderData("ReflectionTest", Vertex, Fragment));
    }

    TEST(InternalShader, CompilerFailureContainsDiagnosticAndCannotPoisonDifferentSource)
    {
        // Catches swallowed compiler errors and a name-only cache serving a stale failed product.
        const auto& broken = GetInternalShaderData(
            "FailureTest", Vertex, "#version 450\nlayout(location=0) out vec4 c; void main(){c=missing_identifier;}");
        ASSERT_TRUE(std::holds_alternative<AssetError>(broken));
        const auto& error = std::get<AssetError>(broken);
        EXPECT_EQ(error.Code, AssetErrorCode::InvalidData);
        EXPECT_NE(error.Message.find("missing_identifier"), std::string::npos);
        EXPECT_NE(error.Message.find("FailureTest"), std::string::npos);
        const auto& corrected = GetInternalShaderData("FailureTest", Vertex, Fragment);
        ASSERT_TRUE(std::holds_alternative<ShaderData>(corrected)) << std::get<AssetError>(corrected).Message;
        EXPECT_NE(std::get<ShaderData>(corrected).Code.Modules[1].Words.size(), 0u);
    }

    TEST(InternalShader, ProjectionAndPixelRowsRespectNonOpenGLCoordinates)
    {
        // Catches hard-coded GL clip depth/Y or editor bottom-left pixels used as upper-left rows.
        using namespace Kans::RenderHardware;
        const auto projection =
            DeviceProjection(glm::mat4(1), {ClipYDirection::Down, DepthRange::ZeroToOne, FramebufferOrigin::UpperLeft});
        EXPECT_EQ(projection * glm::vec4(0.25f, 0.75f, -1, 1), glm::vec4(0.25f, -0.75f, 0, 1));
        EXPECT_EQ(projection * glm::vec4(0.25f, -0.75f, 1, 1), glm::vec4(0.25f, 0.75f, 1, 1));
        EXPECT_EQ(DeviceRow(2, 13, {ClipYDirection::Down, DepthRange::ZeroToOne, FramebufferOrigin::UpperLeft}), 10u);
        EXPECT_EQ(DeviceRow(2, 13, {ClipYDirection::Up, DepthRange::MinusOneToOne, FramebufferOrigin::LowerLeft}), 2u);
    }
} // namespace
