#include <gtest/gtest.h>
#include "Kans3D/RHI/Vulkan/VulkanInternal.h"
#include "Kans3D/RHI/Vulkan/VulkanTestProbes.h"
#include "GpuTestContext.h"

using namespace Kans::RenderHardware;

TEST(VulkanSafety, CpuSelectionRejectsHardwareBeforeDeviceCreation)
{
    // With lavapipe pinned there is no hardware to enumerate. Synthetic types
    // exercise the exact production predicate so removal of the CPU safety gate
    // still fails this test, without ever opening a hardware device.
    EXPECT_TRUE(VulkanDetail::AcceptDeviceType(VK_PHYSICAL_DEVICE_TYPE_CPU, true));
    for (auto type : {VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
                      VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU,
                      VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
                      VK_PHYSICAL_DEVICE_TYPE_OTHER})
    {
        EXPECT_FALSE(VulkanDetail::AcceptDeviceType(type, true));
        EXPECT_TRUE(VulkanDetail::AcceptDeviceType(type, false));
    }
    GpuTestContext context(GpuTestBackend::Vulkan);
    ASSERT_NO_FATAL_FAILURE(context.SetUp());
    auto device = context.CreateDevice();
    auto actual = VulkanTestProbes::Inspect(*device);
    EXPECT_EQ(actual.Type, VK_PHYSICAL_DEVICE_TYPE_CPU);
    EXPECT_GE(actual.ApiVersion, VK_API_VERSION_1_3);
    EXPECT_TRUE(actual.Validation);
    EXPECT_TRUE(actual.SynchronizationValidation);
}

TEST(VulkanSafety, ValidationErrorsReachTheTestSink)
{
    // Catches a disconnected debug messenger (which would silently let missing
    // layout transitions pass). The deliberate synthetic error is collected here;
    // every normal fixture device routes this same callback to ADD_FAILURE.
    GpuTestContext context(GpuTestBackend::Vulkan);
    ASSERT_NO_FATAL_FAILURE(context.SetUp());
    std::vector<std::string> errors;
    VulkanDeviceOptions      options;
    options.Validation = options.SynchronizationValidation = true;
    options.ValidationError = [&](const std::string& message) { errors.push_back(message); };
    auto device             = CreateVulkanDevice({}, options);
    VulkanTestProbes::EmitValidationError(*device, "phase3a validation sink probe");
    device.reset();
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0], "phase3a validation sink probe");
}

TEST(VulkanSafety, CpuForcingOverridesTheEditorsExplicitHardwareOptIn)
{
    // Synthetic types test the production override without enumerating hardware.
    EXPECT_FALSE(VulkanDetail::ResolveCpuRequirement(false, nullptr));
    EXPECT_FALSE(VulkanDetail::ResolveCpuRequirement(false, "0"));
    EXPECT_TRUE(VulkanDetail::ResolveCpuRequirement(true, nullptr));
    const bool required = VulkanDetail::ResolveCpuRequirement(false, "1");
    EXPECT_TRUE(required);
    EXPECT_TRUE(VulkanDetail::AcceptDeviceType(VK_PHYSICAL_DEVICE_TYPE_CPU, required));
    EXPECT_FALSE(VulkanDetail::AcceptDeviceType(VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, required));
    EXPECT_FALSE(VulkanDetail::AcceptDeviceType(VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, required));
}
