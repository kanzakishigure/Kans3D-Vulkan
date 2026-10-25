# ============================================================
# KansEditor — ConsoleApp (C++17)
# ============================================================

file(GLOB_RECURSE EDITOR_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(KansEditor ${EDITOR_SOURCES})

# ---- Include directories (vendor headers propagate via Kans3D PUBLIC links) ----
target_include_directories(KansEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---- Link Kans3D engine (--start-group handles circular refs inside libKans3D.a) ----
if(UNIX AND NOT APPLE)
    target_link_libraries(KansEditor PRIVATE -Wl,--start-group Kans3D -Wl,--end-group)
else()
    target_link_libraries(KansEditor PRIVATE Kans3D)
endif()

# ---- Windows: common dialogs (OpenFileDialog) ----
if(WIN32)
    target_link_libraries(KansEditor PRIVATE comdlg32)
endif()

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

# ---- Post-build: copy assimp DLL to output (Windows only) ----
if(WIN32)
    add_custom_command(TARGET KansEditor POST_BUILD 
        # assimp DLL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<$<CONFIG:Debug>:${VENDOR_DIR}/assimp/bin/Debug/assimp-vc142-mtd.dll>
            $<$<OR:$<CONFIG:Release>,$<CONFIG:Dist>>:${VENDOR_DIR}/assimp/bin/Release/assimp-vc142-mt.dll>
            "$<TARGET_FILE_DIR:KansEditor>"
    )
endif()   
# project Resource
set(DIRS_TO_COPY assets Resources)

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
    COMMAND ${CMAKE_COMMAND} -E echo "Copying Editor resources to $<TARGET_FILE_DIR:KansEditor>/"
)
# ScriptCore is staged into Resources/Scripts by the root CMakeLists (KansEditorScripts).
