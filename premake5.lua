include "dependencies.lua"
workspace "Kans3D"
    architecture"x64"
    startproject "KansEditor"

    configurations 
    { 
        "Debug", 
        "Release",
        "Dist" 
    }
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"





group"Dependencies"
include "Kans3D/vendor/GLFW"
include "Kans3D/vendor/Glad"
include "Kans3D/vendor/imgui"
include "Kans3D/vendor/yaml-cpp"
group""
--相当于将Hazel/vendor/GLFW下的remake5文件直接复制粘贴到此处
group"Core"
project "Kans3D"    
    location"Kans3D"
    kind "StaticLib"
    language "C++"
    cppdialect"c++17"
    staticruntime"off"


    targetdir ("bin/" ..outputdir.. "/%{prj.name}")
    objdir ("bin-int/" ..outputdir.. "/%{prj.name}")
    pchheader "kspch.h"
    pchsource "Kans3D/src/kspch.cpp"
    files 
    { 
        "%{prj.name}/src/**.h", 
        "%{prj.name}/src/**.cpp" ,
        "%{prj.name}/vendor/stb_image/**.h", 
        "%{prj.name}/vendor/stb_image/**.cpp" ,
        "%{prj.name}/vendor/glm/glm/**.hpp",
        "%{prj.name}/vendor/glm/glm/**.inl"
    }
    includedirs 
    { 
        "%{prj.name}/src",
        "%{prj.name}/vendor",
        

        "%{IncludeDir.GLFW}",
        "%{IncludeDir.Glad}",
        "%{IncludeDir.ImGui}",
        "%{IncludeDir.GLM}",
        "%{IncludeDir.stb_image}",
        "%{IncludeDir.entt}",
        "%{IncludeDir.assimp}",
        "%{IncludeDir.VulkanSDK}",
        "%{IncludeDir.yaml_cpp}",
        "%{IncludeDir.mono}",
        "%{IncludeDir.shaderc_util}"
        
    }
    links
    {
        "GLFW",
        "Glad",
        "ImGui",
        "opengl32.lib",
        "yaml-cpp",
        "%{Library.Vulkan}",
        "%{Library.mono}"
        
    }
    filter "system:windows" 
        systemversion "latest" 

        defines
        {
            "PLATFORM_WINDOWS",
            "GLFW_INCLUDE_NONE",
            "_CRT_SECURE_NO_WARNINGS"
        }



    filter "configurations:Debug"
        defines 
        {
            "KS_DEBUG" 
        }
        runtime "Debug"
        symbols "on"
        links
        {
            "%{Library.ShaderC_Debug}",
			"%{Library.ShaderC_Utils_Debug}",
			"%{Library.SPIRV_Cross_Debug}",
			"%{Library.SPIRV_Cross_GLSL_Debug}",
			"%{Library.SPIRV_Tools_Debug}",
        }

    filter "configurations:Release"
        defines 
        {
            "KS_RELEASE" 
        }
        runtime "Release"
        optimize "on"
        links
		{

			"%{Library.ShaderC_Release}",
			"%{Library.ShaderC_Utils_Release}",
			"%{Library.SPIRV_Cross_Release}",
			"%{Library.SPIRV_Cross_GLSL_Release}",
		}
    filter "configurations:Dist"
        defines  
        {
            "KS_DIST" 
        }
        runtime "Release"
        optimize "On"
		symbols "Off"
        removefiles
        {

        }
project "KansScriptCore"
	location "KansScriptCore"
	kind "SharedLib"
	language "C#"
	dotnetframework "4.8"
	
	targetdir ("KansEditor/Resources/Scripts")
	objdir ("KansEditor/Resources/Scripts/Intermediates")

	files 
	{
		"%{prj.name}/Source/**.cs", 
	}        
group""
group"Editor"
project "KansEditor"
        location"KansEditor"  
        kind "ConsoleApp"
        language "C++"
        cppdialect "c++17"     
        staticruntime "off"
             
        targetdir ("bin/" ..outputdir.. "/%{prj.name}")
        objdir ("bin-int/" ..outputdir.. "/%{prj.name}")
    
        files
        { 
            "%{prj.name}/src/**.h", 
            "%{prj.name}/src/**.cpp" ,

            -- Shaders
            --"%{prj.name}/Resources/Shaders/**.glsl", 
            --"%{prj.name}/Resources/Shaders/**.glslh", 
        }
        includedirs 
        { 
            "%{prj.name}/src",
            "Kans3D/src",
            "Kans3D/vendor",

            "%{IncludeDir.GLM}",
            "%{IncludeDir.ImGui}",
            "%{IncludeDir.entt}",
            "%{IncludeDir.assimp}",
            "%{IncludeDir.yaml_cpp}",

            
        }
        links
        {
            "Kans3D"
        }
        filter "system:windows"
            systemversion "latest" 
    
            defines
            {
                "HZ_PLATFORM_WINDOWS"      
            }
    
    
    
        filter "configurations:Debug"
            defines "KS_DEBUG" 
            runtime "Debug"
            symbols "on"
            links
            {
                "%{Library.Assimp_Debug}"
            }
            postbuildcommands 
            {
                '{COPY} "%{Binaries.Assimp_Debug}" "%{cfg.targetdir}"',
                '{COPY} "../kans3D/vendor/mono/bin/Debug/mono-2.0-sgen.dll" "%{cfg.targetdir}"'
            }
           
        filter "configurations:Release"
            defines  "KS_RELEASE" 
            runtime "Release"
            optimize "on"
            links
            {
                "%{Library.Assimp_Release}"
            }
            postbuildcommands 
            {
                '{COPY} "%{Binaries.Assimp_Release}" "%{cfg.targetdir}"',
                '{COPY} "../kans3D/vendor/mono/bin/Release/mono-2.0-sgen.dll" "%{cfg.targetdir}"'
            }
            
        filter "configurations:Dist"
            defines  "KS_DIST" 
            runtime "Release"
            optimize "on"
            links
            {
                "%{Library.Assimp_Release}"
            }
            postbuildcommands 
            {
                '{COPY} "%{Binaries.Assimp_Release}" "%{cfg.targetdir}"',
                '{COPY} "../kans3D/vendor/mono/bin/Release/mono-2.0-sgen.dll" "%{cfg.targetdir}"'
            }
group""