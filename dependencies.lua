--include 文件夹 采用相对路径
IncludeDir ={}
IncludeDir["GLFW"] = "Kans3D/vendor/GLFW/include"
IncludeDir["Glad"] = "Kans3D/vendor/Glad/include"
IncludeDir["ImGui"] = "Kans3D/vendor/imgui"
IncludeDir["GLM"] = "Kans3D/vendor/glm"
IncludeDir["stb_image"] = "kans3D/vendor/stb_image"
IncludeDir["entt"] = "Kans3D/vendor/entt/include"
IncludeDir["assimp"] = "Kans3D/vendor/assimp/include"
IncludeDir["VulkanSDK"] = "Kans3D/vendor/VulkanSDK/Include"
IncludeDir["yaml_cpp"] = "Kans3D/vendor/yaml-cpp/include"
IncludeDir["mono"] = "Kans3D/vendor/mono/include"

LibraryDir = {}
LibraryDir["VulkanSDK"] = "%{_WORKING_DIR}/Kans3D/vendor/VulkanSDK/Lib"
LibraryDir["Mono"] = "%{_WORKING_DIR}/Kans3D/vendor/mono/lib/%{cfg.buildcfg}"

Library = {}
Library["Assimp_Debug"] = "%{_WORKING_DIR}/Kans3D/vendor/assimp/bin/Debug/assimp-vc142-mtd.lib"
Library["Assimp_Release"] = "%{_WORKING_DIR}/Kans3D/vendor/assimp/bin/Release/assimp-vc142-mt.lib"
Library["Vulkan"] = "%{LibraryDir.VulkanSDK}/vulkan-1.lib"
Library["mono"] = "%{LibraryDir.Mono}/mono-2.0-sgen.lib"

Binaries = {}
Binaries["Assimp_Debug"] = "%{_WORKING_DIR}/Kans3D/vendor/assimp/bin/Debug/assimp-vc142-mtd.dll"
Binaries["Assimp_Release"] = "%{_WORKING_DIR}/Kans3D/vendor/assimp/bin/Release/assimp-vc142-mt.dll"