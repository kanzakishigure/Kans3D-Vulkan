# ============================================================
# Kans3D 鈥? StaticLib (C++17, PCH: kspch.h)
# premake: project "Kans3D", location "Kans3D"
# ============================================================
set(KANS3D_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(KANS3D_VENDOR ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

# ---- Sources ----
file(GLOB_RECURSE KANS3D_HEADERS ${KANS3D_SRC}/*.h)
file(GLOB_RECURSE KANS3D_SOURCES ${KANS3D_SRC}/*.cpp)

add_library(Kans3D STATIC
    ${KANS3D_HEADERS}
    ${KANS3D_SOURCES}
)

# The pinned ImGui Vulkan backend uses immutable font samplers for user images.
# Compile an adapter copy with ordinary samplers, keeping the vendored submodule
# untouched. Fail configuration if an ImGui upgrade changes these patch sites.
set(KANS_IMGUI_VULKAN_SOURCE "${KANS3D_VENDOR}/imgui/backends/imgui_impl_vulkan.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${KANS_IMGUI_VULKAN_SOURCE}")
file(READ "${KANS_IMGUI_VULKAN_SOURCE}" KANS_IMGUI_VULKAN_TEXT)
string(REGEX MATCHALL "pImmutableSamplers = sampler" KANS_IMGUI_SAMPLER_SITES "${KANS_IMGUI_VULKAN_TEXT}")
list(LENGTH KANS_IMGUI_SAMPLER_SITES KANS_IMGUI_SAMPLER_SITE_COUNT)
if(NOT KANS_IMGUI_SAMPLER_SITE_COUNT EQUAL 2)
    message(FATAL_ERROR "Review Kans3D's ImGui Vulkan sampler adapter for this ImGui version")
endif()
string(REPLACE "pImmutableSamplers = sampler;" "pImmutableSamplers = nullptr;"
    KANS_IMGUI_VULKAN_TEXT "${KANS_IMGUI_VULKAN_TEXT}")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/imgui-vulkan")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/imgui-vulkan/imgui_impl_vulkan_kans.cpp" "${KANS_IMGUI_VULKAN_TEXT}")
target_include_directories(Kans3D PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/imgui-vulkan" "${KANS3D_VENDOR}/imgui/backends")

# ---- Precompiled header ----
target_precompile_headers(Kans3D PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${KANS3D_SRC}/kspch.h>
)

# ---- Compile definitions ----
target_compile_definitions(Kans3D PUBLIC
    GLFW_INCLUDE_NONE
)
if(WIN32)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(Kans3D PUBLIC /Zc:preprocessor /utf-8)
    endif()
    target_compile_definitions(Kans3D PUBLIC
        PLATFORM_WINDOWS
        _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(Kans3D PUBLIC opengl32.lib)
elseif(LINUX)
    target_compile_definitions(Kans3D PUBLIC
        PLATFORM_LINUX)
endif()

# ---- Include directories (engine only; vendor headers come via targets) ----
target_include_directories(Kans3D PUBLIC
    ${KANS3D_SRC}
    ${KANS3D_VENDOR}
)

# ---- Link vendor dependencies (all defined in vendor/CMakeLists.txt) ----
target_link_libraries(Kans3D PUBLIC
    glfw
    Glad
    ImGui
    yaml-cpp
    stb_image
    glm
    EnTT
    ImGuizmo
    assimp
    mono
    VulkanMemoryAllocator
)

# Vulkan SDK (shaderc, spirv-cross, vulkan loader)
if(VULKAN_SDK_DIR)
    target_link_libraries(Kans3D PUBLIC vulkan_sdk)
endif()

# ---- Output & properties ----
kans_set_output_dirs(Kans3D)
set_target_properties(Kans3D PROPERTIES
    FOLDER "Core"
    CXX_STANDARD 17
    MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
)
