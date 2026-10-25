#include <glad/glad.h>
#include <gtest/gtest.h>
#include "GpuTestContext.h"

// The parameterized conformance suite tests neutral ownership and retirement.
// This extra backend test probes actual native deletion, independently of the
// backend's counters: an early delete with unchanged counters must also fail.
TEST(OpenGLRHILifetime, NativeTextureAndImGuiIDSurviveReleaseUntilFrameRetirement)
{
    GpuTestContext context(GpuTestBackend::OpenGL);
    ASSERT_NO_FATAL_FAILURE(context.SetUp());
    auto device = context.CreateDevice();
    device->BeginFrame();
    Kans::RenderHardware::TextureDesc desc;
    desc.Usage         = Kans::RenderHardware::TextureUsage::Sampled;
    auto       texture = device->CreateTexture(desc);
    const auto id      = device->GetImGuiTextureID(texture);
    ASSERT_NE(id, 0u);
    EXPECT_EQ(glIsTexture(static_cast<GLuint>(id)), GL_TRUE);
    texture.reset();
    EXPECT_EQ(glIsTexture(static_cast<GLuint>(id)), GL_TRUE);
    device->EndFrame();
    EXPECT_EQ(glIsTexture(static_cast<GLuint>(id)), GL_FALSE);
    device.reset();
}
