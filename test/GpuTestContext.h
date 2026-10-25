#pragma once
#include <memory>
#include "Kans3D/RHI/RHI.h"

enum class GpuTestBackend
{
    OpenGL,
    Vulkan
};

// Owns a headless context. Destroy GPU resources before TearDown (also called by
// the destructor). SetUp reports fatal gtest failures; callers must propagate
// them with ASSERT_NO_FATAL_FAILURE. Backend selection is deliberately explicit.
class GpuTestContext
{
public:
    explicit GpuTestContext(GpuTestBackend backend);
    ~GpuTestContext();
    GpuTestContext(const GpuTestContext&)            = delete;
    GpuTestContext& operator=(const GpuTestContext&) = delete;
    void            SetUp();
    void            TearDown();
    std::unique_ptr<Kans::RenderHardware::Device> CreateDevice(const Kans::RenderHardware::DeviceDesc& desc = {});

private:
    struct State;
    std::unique_ptr<State> m_State;
    GpuTestBackend         m_Backend;
};
