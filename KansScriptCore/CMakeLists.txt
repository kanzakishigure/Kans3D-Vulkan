# ============================================================
# KansScriptCore — C# SharedLib (.NET Framework 4.8)
# premake: project "KansScriptCore", location "KansScriptCore"
#          kind "SharedLib", language "C#", dotnetframework "4.8"
#          targetdir "KansEditor/Resources/Scripts"
#          objdir    "KansEditor/Resources/Scripts/Intermediates"
#
# NOTE: CMake does not natively build C# projects.
# This CMakeLists.txt documents the target and can be extended
# with a custom command using MSBuild / dotnet CLI if desired.
# ============================================================


cmake_minimum_required(VERSION 3.10)
enable_language(CSharp)
set(SCRIPTCORE_SRCDIR       ${CMAKE_CURRENT_SOURCE_DIR}/Source)
file(GLOB_RECURSE SCRIPTCORE_SOURCES CONFIGURE_DEPENDS ${SCRIPTCORE_SRCDIR}/*.cs)

add_library(ScriptCore SHARED 
 ${SCRIPTCORE_SOURCES})
set_target_properties(ScriptCore PROPERTIES LANGUAGE CSharp)

