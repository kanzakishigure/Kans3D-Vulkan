# ============================================================
# KansEditor — ConsoleApp (C++17)
# premake: project "KansEditor", location "KansEditor"
# ============================================================

file(GLOB_RECURSE EDITOR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(KansEditor ${EDITOR_SOURCES})
add_dependencies(KansEditor ScriptCore) 
# ---- Include directories (vendor headers propagate via Kans3D PUBLIC links) ----
target_include_directories(KansEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---- Link Kans3D engine ----
target_link_libraries(KansEditor PRIVATE Kans3D)

# ---- assimp DLL is now linked via Kans3D -> assimp (IMPORTED) target ----
# (KansEditor still needs to copy the DLL at post-build)

# ---- Output & properties ----
kans_set_output_dirs(KansEditor)
set_target_properties(KansEditor PROPERTIES
    FOLDER "Editor"
    CXX_STANDARD 17
    MSVC_RUNTIME_LIBRARY "MultiThreadedDLL"
    VS_DEBUGGER_WORKING_DIRECTORY "${OUTDIR_BASE_Debug}/KansEditor"
)

# ---- Post-build: copy assimp DLL + mono DLL to output ----
# premake postbuildcommands:
#   {COPY} "%{Binaries.Assimp_Debug}" "%{cfg.targetdir}"
#   {COPY} "../kans3D/vendor/mono/bin/Debug/mono-2.0-sgen.dll" "%{cfg.targetdir}"
add_custom_command(TARGET KansEditor POST_BUILD 
    # assimp DLL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<$<CONFIG:Debug>:${VENDOR_DIR}/assimp/bin/Debug/assimp-vc142-mtd.dll>
        $<$<OR:$<CONFIG:Release>,$<CONFIG:Dist>>:${VENDOR_DIR}/assimp/bin/Release/assimp-vc142-mt.dll>
        "$<TARGET_FILE_DIR:KansEditor>"
    # mono DLL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${VENDOR_DIR}/mono/bin/$<$<CONFIG:Debug>:Debug>$<$<OR:$<CONFIG:Release>,$<CONFIG:Dist>>:Release>/mono-2.0-sgen.dll
        "$<TARGET_FILE_DIR:KansEditor>"
)   
# project Resource
set(DIRS_TO_COPY assets mono Resources)

foreach(DIR ${DIRS_TO_COPY})                                                                                                       
    add_custom_command(TARGET KansEditor POST_BUILD                                                                                
        COMMAND ${CMAKE_COMMAND}                                                                                                   
            -DSRC_DIR=${KANSEDITOR_DIR}/${DIR}                                                                                     
            -DDST_DIR=$<TARGET_FILE_DIR:KansEditor>/${DIR}                                                                         
            -DDIR_NAME=${DIR}                                                                                                      
            -P "${CMAKE_CURRENT_SOURCE_DIR}/copy_Resource.cmake"                                                                     
        VERBATIM                                                                                                                   
    )                                                                                                                              
endforeach()        

add_custom_command(TARGET KansEditor POST_BUILD 
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${KANSEDITOR_DIR}/KansEditor.ini
        "$<TARGET_FILE_DIR:KansEditor>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${KANSEDITOR_DIR}/Project.ini
        "$<TARGET_FILE_DIR:KansEditor>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${KANSEDITOR_DIR}/imgui.ini
        "$<TARGET_FILE_DIR:KansEditor>"

    COMMAND ${CMAKE_COMMAND} -E echo "Copying Editor resources to $<TARGET_FILE_DIR:KansEditor>/"
)
#Script Core
add_custom_command(TARGET KansEditor POST_BUILD
   
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ScriptCore>"
        "$<TARGET_FILE_DIR:KansEditor>/Resources/Scripts/ScriptCore.dll"
        
    COMMAND ${CMAKE_COMMAND} -E echo "Copying ScriptCore to $<TARGET_FILE_DIR:KansEditor>/Resources/Scripts"
    
)
