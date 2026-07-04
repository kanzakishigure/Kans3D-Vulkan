# ============================================================
# Vendor CMakeLists.txt — Centralized vendor build definitions
#
# All vendor targets (static libs, header-only, imported, pre-built)
# are defined here instead of inside submodule directories, so that
# git submodule state is never dirtied by build-system files.
# ============================================================

# ============================================================
# GLFW — uses its own CMakeLists.txt (part of the GLFW submodule)
# ============================================================
# Disable GLFW examples & tests (they need dwmapi.lib, we only need the lib)


set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
add_subdirectory(GLFW)
if(WIN32)
    target_link_libraries(glfw PRIVATE dwmapi.lib)
endif()
set_target_properties(glfw PROPERTIES
    FOLDER "Dependencies"
)
# ============================================================
# Glad — StaticLib (C)
# ============================================================
add_library(Glad STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Glad/include/glad/glad.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Glad/include/KHR/khrplatform.h
    ${CMAKE_CURRENT_SOURCE_DIR}/Glad/src/glad.c
)
target_include_directories(Glad PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Glad/include)
kans_set_output_dirs(Glad)
set_target_properties(Glad PROPERTIES
    FOLDER "Dependencies"
    MSVC_RUNTIME_LIBRARY "MultiThreaded" 
)

# ============================================================
# ImGui — StaticLib (C++17)
# ============================================================
add_library(ImGui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imconfig.h
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.h
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_internal.h
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imstb_rectpack.h
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imstb_textedit.h
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imstb_truetype.h
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_demo.cpp
)
target_include_directories(ImGui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
kans_set_output_dirs(ImGui)
set_target_properties(ImGui PROPERTIES
    FOLDER "Dependencies"
    CXX_STANDARD 17
)
if(UNIX)
    set_target_properties(ImGui PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# ============================================================
# yaml-cpp — StaticLib (C++17)
# ============================================================
file(GLOB_RECURSE YAML_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/yaml-cpp/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/yaml-cpp/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/yaml-cpp/include/*.h
)
add_library(yaml-cpp STATIC ${YAML_SOURCES})
target_include_directories(yaml-cpp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/yaml-cpp/include)
kans_set_output_dirs(yaml-cpp)
set_target_properties(yaml-cpp PROPERTIES
    FOLDER "Dependencies"
    CXX_STANDARD 17
)
if(UNIX)
    set_target_properties(yaml-cpp PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# ============================================================
# glm — Header-only INTERFACE library
# ============================================================
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/glm)


# ============================================================
# EnTT — Header-only INTERFACE library
# ============================================================
add_library(EnTT INTERFACE)
target_include_directories(EnTT INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/entt/include)

# ============================================================
# stb_image — StaticLib (compiled separately, no PCH dependency)
# ============================================================
add_library(stb_image STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/stb_image/stb_image.h
    ${CMAKE_CURRENT_SOURCE_DIR}/stb_image/stb_image.cpp
)
target_include_directories(stb_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/stb_image)
kans_set_output_dirs(stb_image)
set_target_properties(stb_image PROPERTIES
    FOLDER "Dependencies"
    CXX_STANDARD 17
)

# ============================================================
# ImGuizmo — StaticLib (depends on ImGui)
# ============================================================
add_library(ImGuizmo STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ImGuizmo/ImGuizmo.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ImGuizmo/ImGuizmo.cpp
)
target_include_directories(ImGuizmo PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ImGuizmo)
target_link_libraries(ImGuizmo PUBLIC ImGui)
kans_set_output_dirs(ImGuizmo)
set_target_properties(ImGuizmo PROPERTIES
    FOLDER "Dependencies"
    CXX_STANDARD 17
)

# ============================================================
# assimp — Pre-built IMPORTED library
# ============================================================
add_library(assimp INTERFACE)                                                                                                                                                                                              
target_include_directories(assimp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/assimp/include)                                                                                                                                    
target_link_libraries(assimp INTERFACE                                                                                                                                                                                     
    "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/assimp/bin/Debug/assimp-vc142-mtd.lib>"                                                                                                                                 
    "$<$<OR:$<CONFIG:Release>,$<CONFIG:Dist>>:${CMAKE_CURRENT_SOURCE_DIR}/assimp/bin/Release/assimp-vc142-mt.lib>"                                                                                                         
)     

set_target_properties(assimp PROPERTIES
    FOLDER "Dependencies"
    CXX_STANDARD 17
)
# ============================================================
# mono — Pre-built IMPORTED library (config-dependent path)
# ============================================================
add_library(mono INTERFACE)                                                                                                                                                                                                
target_include_directories(mono INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/mono/include)                                                                                                                                        
target_link_libraries(mono INTERFACE                                                                                                                                                                                       
    "$<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/mono/lib/Debug/mono-2.0-sgen.lib>"                                                                                                                                      
    "$<$<OR:$<CONFIG:Release>,$<CONFIG:Dist>>:${CMAKE_CURRENT_SOURCE_DIR}/mono/lib/Release/mono-2.0-sgen.lib>"                                                                                                             
)       

# ============================================================
# Vulkan SDK — INTERFACE library bundling shaderc, spirv-cross, vulkan
# ============================================================
if(VULKAN_SDK_DIR)
    add_library(vulkan_sdk INTERFACE)
    target_link_libraries(vulkan_sdk INTERFACE
        ${VULKAN_SDK_DIR}/Lib/shaderc_combined.lib
        ${VULKAN_SDK_DIR}/Lib/spirv-cross-core.lib
        ${VULKAN_SDK_DIR}/Lib/spirv-cross-glsl.lib
        ${VULKAN_SDK_DIR}/Lib/SPIRV-Tools$<$<CONFIG:Debug>:d>.lib
        ${VULKAN_SDK_DIR}/Lib/vulkan-1.lib
    )
    target_include_directories(vulkan_sdk INTERFACE ${VULKAN_SDK_DIR}/Include
                                                    ${CMAKE_CURRENT_SOURCE_DIR}/shaderc/libshaderc_util/include)
endif()

# ============================================================
# spdlog — header-only INTERFACE library 
# ============================================================
 add_library(spdlog INTERFACE)                                                                                                                                                                                              
target_include_directories(spdlog INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/spdlog)
