#pragma once
#include <memory>

enum class GpuTestBackend
{
    OpenGL
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

private:
    struct State;
    std::unique_ptr<State> m_State;
    GpuTestBackend         m_Backend;
};
