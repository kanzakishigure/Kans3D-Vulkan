

VULKAN_SDK = os.getenv("VULKAN_SDK")
--include dir
IncludeDir ={}
IncludeDir["GLFW"] = "Kans3D/vendor/GLFW/include"
IncludeDir["Glad"] = "Kans3D/vendor/Glad/include"
IncludeDir["ImGui"] = "Kans3D/vendor/imgui"
IncludeDir["GLM"] = "Kans3D/vendor/glm"
IncludeDir["stb_image"] = "kans3D/vendor/stb_image"
IncludeDir["entt"] = "Kans3D/vendor/entt/include"
IncludeDir["assimp"] = "Kans3D/vendor/assimp/include"
IncludeDir["yaml_cpp"] = "Kans3D/vendor/yaml-cpp/include"
IncludeDir["mono"] = "Kans3D/vendor/mono/include"
IncludeDir["VulkanSDK"] = "%{VULKAN_SDK}/Include"
IncludeDir["shaderc_util"] = "%{wks.location}/Kans3D/vendor/shaderc/libshaderc_util/include"

LibraryDir = {}
LibraryDir["VulkanSDK"] = "%{VULKAN_SDK}/Lib"
LibraryDir["Mono"] = "%{_WORKING_DIR}/Kans3D/vendor/mono/lib/%{cfg.buildcfg}"

Library = {}
Library["Assimp_Debug"] = "%{_WORKING_DIR}/Kans3D/vendor/assimp/bin/Debug/assimp-vc142-mtd.lib"
Library["Assimp_Release"] = "%{_WORKING_DIR}/Kans3D/vendor/assimp/bin/Release/assimp-vc142-mt.lib"
Library["Vulkan"] = "%{LibraryDir.VulkanSDK}/vulkan-1.lib"
Library["mono"] = "%{LibraryDir.Mono}/mono-2.0-sgen.lib"

Library["ShaderC_Debug"] = "%{LibraryDir.VulkanSDK}/shaderc_sharedd.lib"
Library["ShaderC_Utils_Debug"] = "%{LibraryDir.VulkanSDK}/shaderc_utild.lib"
Library["SPIRV_Cross_Debug"] = "%{LibraryDir.VulkanSDK}/spirv-cross-cored.lib"
Library["SPIRV_Cross_GLSL_Debug"] = "%{LibraryDir.VulkanSDK}/spirv-cross-glsld.lib"
Library["SPIRV_Tools_Debug"] = "%{LibraryDir.VulkanSDK}/SPIRV-Toolsd.lib"

Library["ShaderC_Release"] = "%{LibraryDir.VulkanSDK}/shaderc_shared.lib"
Library["ShaderC_Utils_Release"] = "%{LibraryDir.VulkanSDK}/shaderc_util.lib"
Library["SPIRV_Cross_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-core.lib"
Library["SPIRV_Cross_GLSL_Release"] = "%{LibraryDir.VulkanSDK}/spirv-cross-glsl.lib"
Library["SPIRV_Tools_Release"] = "%{LibraryDir.VulkanSDK}/SPIRV-Tools.lib"


Binaries = {}
Binaries["Assimp_Debug"] = "%{_WORKING_DIR}/Kans3D/vendor/assimp/bin/Debug/assimp-vc142-mtd.dll"
Binaries["Assimp_Release"] = "%{_WORKING_DIR}/Kans3D/vendor/assimp/bin/Release/assimp-vc142-mt.dll"