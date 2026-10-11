find_package(GTest REQUIRED)

# Test discovery runs the executable immediately after linking.
function(kans_copy_test_runtime target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VENDOR_DIR}/assimp/bin/$<IF:$<CONFIG:Debug>,Debug,Release>/assimp-vc142-$<IF:$<CONFIG:Debug>,mtd,mt>.dll"
                "$<TARGET_FILE_DIR:${target}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${VENDOR_DIR}/mono/bin/$<IF:$<CONFIG:Debug>,Debug,Release>/mono-2.0-sgen.dll"
                "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()
add_executable(Kans3DTests
    CoreIdentityConcurrencyTests.cpp
    ModelLoadingTests.cpp
    ImportedModelDataTests.cpp
    LocalIDMapTests.cpp
    MeshProductTests.cpp
    ModelProductTests.cpp
    SceneProductModelTests.cpp
    SceneHierarchyTests.cpp
    SceneHistoryTests.cpp
    ScenePlaySessionTests.cpp
    EditorCameraTests.cpp
    DropPlacementTests.cpp
    EditorAssetImportServiceTests.cpp
    ModelImporterTests.cpp
    ModelMaterialRemappingTests.cpp
    AssetRegistryTests.cpp
    ProductStoreTests.cpp
    AssetManagerTests.cpp
    RenderAssetCacheTests.cpp
    TextureProductTests.cpp
    ShaderProductTests.cpp
    ShaderImporterTests.cpp
    MaterialBindingsTests.cpp
    MaterialSourceTests.cpp
    MaterialInstanceTests.cpp
    MaterialParameterGroupsTests.cpp
    TextureImporterTests.cpp
    ImporterRegistryTests.cpp
    ImportFingerprintTests.cpp
    ProjectBuildTests.cpp
    SourceAssetIDTests.cpp
    AssetIDTests.cpp
    AssetPathTests.cpp
    SourceAssetMetadataTests.cpp
    SourceAssetMetaSerializerTests.cpp
    SourceAssetDatabaseTests.cpp
    AssetScannerTests.cpp
    SourceAssetDatabaseSerializerTests.cpp
    ProjectTests.cpp
    ContentBrowserTests.cpp
    ../KansEditor/src/EditorAssetCachePath.cpp
    ../KansEditor/src/Panels/ContentBrowser/BrowserModel.cpp
    ../KansEditor/src/Panels/ContentBrowser/AssetFileOperations.cpp
    ../KansEditor/src/SceneHistory.cpp
)
target_link_libraries(Kans3DTests PRIVATE Kans3D GTest::gtest_main)
target_compile_features(Kans3DTests PRIVATE cxx_std_17)
target_compile_definitions(Kans3DTests PRIVATE
    KANS_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../KansEditor/assets/model")
include(GoogleTest)
kans_copy_test_runtime(Kans3DTests)
gtest_discover_tests(Kans3DTests)

# Real GL tests run separately from the CPU-only suite.
add_executable(OpenGLTextureTests OpenGLTextureTests.cpp RenderAssetCacheOpenGLTests.cpp)
target_link_libraries(OpenGLTextureTests PRIVATE Kans3D GTest::gtest_main)
target_compile_definitions(OpenGLTextureTests PRIVATE
    KANS_TEST_ICON_DIR="${PROJECT_SOURCE_DIR}/KansEditor/Resources/Editor/ContentBrowser"
    KANS_TEST_SHADER_DIR="${PROJECT_SOURCE_DIR}/KansEditor/Resources/Shaders/OpenGL"
    KANS_TEST_EDITOR_ASSET_DIR="${PROJECT_SOURCE_DIR}/KansEditor/assets")
if(UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED COMPONENTS EGL)
    target_link_libraries(OpenGLTextureTests PRIVATE OpenGL::EGL)
    target_compile_definitions(OpenGLTextureTests PRIVATE KANS_TEST_USE_EGL)
endif()
kans_copy_test_runtime(OpenGLTextureTests)
if(UNIX AND NOT APPLE)
    # Use Mesa's software rasterizer one test at a time: parallel GL tests on a
    # hardware driver have hung the GPU.
    gtest_discover_tests(OpenGLTextureTests PROPERTIES
        LABELS "OpenGL"
        RUN_SERIAL TRUE
        ENVIRONMENT "LIBGL_ALWAYS_SOFTWARE=1;MESA_LOADER_DRIVER_OVERRIDE=llvmpipe;GALLIUM_DRIVER=llvmpipe")
else()
    gtest_discover_tests(OpenGLTextureTests PROPERTIES LABELS "OpenGL")
endif()
if(UNIX AND NOT APPLE)
    # Mesa otherwise returns its highest supported core version, masking the
    # editor's GL 4.3 + ARB_direct_state_access loader path.
    add_test(NAME OpenGLTextureMipmap.GL43ExtensionPath COMMAND OpenGLTextureTests)
    set_tests_properties(OpenGLTextureMipmap.GL43ExtensionPath PROPERTIES
        ENVIRONMENT "MESA_GL_VERSION_OVERRIDE=4.3;LIBGL_ALWAYS_SOFTWARE=1;MESA_LOADER_DRIVER_OVERRIDE=llvmpipe"
        RUN_SERIAL TRUE
        LABELS "OpenGL")
endif()
