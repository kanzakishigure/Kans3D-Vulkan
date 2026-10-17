#include "GpuTestContext.h"
#include <cstdlib>
#include <gtest/gtest.h>
#include "Kans3D/RHI/Backend.h"
#include "Kans3D/RHI/Vulkan/VulkanDevice.h"

GpuTestContext::GpuTestContext(GpuTestBackend backend) : m_Backend(backend) {}
GpuTestContext::~GpuTestContext() { TearDown(); }
void GpuTestContext::SetUp()
{
    // Before even constructing an instance, refuse an unpinned test process.
    const char* driver = std::getenv("VK_DRIVER_FILES");
    const char* legacy = std::getenv("VK_ICD_FILENAMES");
    ASSERT_NE(driver, nullptr);
    ASSERT_NE(legacy, nullptr);
    ASSERT_STREQ(driver, "/usr/share/vulkan/icd.d/lvp_icd.json");
    ASSERT_STREQ(legacy, "/usr/share/vulkan/icd.d/lvp_icd.json");
    Kans::RenderHardware::InitializeHeadlessDevice(Kans::RenderHardware::Backend::Vulkan, CreateDevice());
    m_Initialized = true;
}
void GpuTestContext::TearDown()
{
    if (!m_Initialized)
        return;
    Kans::RenderHardware::ShutdownDevice();
    m_Initialized = false;
}
std::unique_ptr<Kans::RenderHardware::Device> GpuTestContext::CreateDevice(const Kans::RenderHardware::DeviceDesc& desc)
{
    Kans::RenderHardware::VulkanDeviceOptions options;
    options.RequireCpuDevice          = true;
    options.Validation                = true;
    options.SynchronizationValidation = true;
    // gtest records failures from driver callback threads too. This sink
    // remains valid through resource/device/instance destruction.
    options.ValidationError = [](const std::string& message) { ADD_FAILURE() << message; };
    return Kans::RenderHardware::CreateVulkanDevice(desc, options);
}
