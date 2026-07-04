# ============================================================
# Kans3D — StaticLib (C++17, PCH: kspch.h)
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

# ---- Precompiled header ----
target_precompile_headers(Kans3D PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${KANS3D_SRC}/kspch.h>
)

# ---- Compile definitions ----
if(WIN32)
    target_compile_definitions(Kans3D PUBLIC
        GLFW_INCLUDE_NONE
        PLATFORM_WINDOWS
        _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(Kans3D PUBLIC opengl32.lib)
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
