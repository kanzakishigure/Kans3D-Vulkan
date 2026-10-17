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
    LightingHdrTests.cpp
    BackendSelectionTests.cpp
    RenderGraphTests.cpp
    RenderViewTests.cpp
    RenderWorldTests.cpp
    ReferenceImageTests.cpp
    ReferenceImage.cpp
    CoreIdentityConcurrencyTests.cpp
    ModelLoadingTests.cpp
    ImportedModelDataTests.cpp
    LocalIDMapTests.cpp
    MeshProductTests.cpp
    ModelProductTests.cpp
    SceneProductModelTests.cpp
    PrefabSystemTests.cpp
    SceneHierarchyTests.cpp
    SceneHistoryTests.cpp
    ScenePlaySessionTests.cpp
    EditorCameraTests.cpp
    DropPlacementTests.cpp
    EditorAssetImportServiceTests.cpp
    ModelImporterTests.cpp
    ModelSectionsTests.cpp
    ModelMaterialRemappingTests.cpp
    AssetRegistryTests.cpp
    ProductStoreTests.cpp
    AssetManagerTests.cpp
    RenderAssetCacheTests.cpp
    TextureProductTests.cpp
    MultiPassShaderTests.cpp
    ShaderProductTests.cpp
    ShaderImporterTests.cpp
    InternalShaderTests.cpp
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
    ../KansEditor/src/Panels/ContentBrowser/BrowserModel.cpp
    ../KansEditor/src/Panels/ContentBrowser/AssetFileOperations.cpp
    ../KansEditor/src/SceneHistory.cpp
)
find_package(PNG REQUIRED)
target_link_libraries(Kans3DTests PRIVATE Kans3D GTest::gtest_main PNG::PNG)
target_compile_features(Kans3DTests PRIVATE cxx_std_17)
target_compile_definitions(Kans3DTests PRIVATE
    KANS_TEST_MULTIPASS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/multipass"
    KANS_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../KansEditor/assets/model")
include(GoogleTest)
kans_copy_test_runtime(Kans3DTests)
gtest_discover_tests(Kans3DTests)

# Keep the allocation-counting operator new local to the feature CPU tests.
add_executable(RenderFeatureTests RenderFeatureTests.cpp)
target_link_libraries(RenderFeatureTests PRIVATE Kans3D GTest::gtest_main)
target_compile_features(RenderFeatureTests PRIVATE cxx_std_17)
kans_copy_test_runtime(RenderFeatureTests)
gtest_discover_tests(RenderFeatureTests)

# GPU tests use lavapipe serially, with validation enabled by GpuTestContext.
add_executable(GpuTests
    GpuTestContext.cpp
    ReferenceImage.cpp
    RendererReferenceTests.cpp
    RendererServicesTests.cpp
    ScenePassTests.cpp
    EditorTextureGpuTests.cpp
    EditorStartupGpuTests.cpp
    ../KansEditor/src/Panels/ContentBrowser/BrowserModel.cpp
    ../KansEditor/src/Panels/ContentBrowser/ThumbnailCache.cpp
    TextureGpuTests.cpp
    RenderAssetCacheGpuTests.cpp)
target_link_libraries(GpuTests PRIVATE Kans3D GTest::gtest_main PNG::PNG)
target_compile_definitions(GpuTests PRIVATE
    KANS_TEST_MULTIPASS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/multipass"
    KANS_TEST_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/references"
    KANS_TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/reference-output"
    KANS_TEST_SHADER_DIR="${PROJECT_SOURCE_DIR}/KansEditor/Resources/Shaders"
    KANS_TEST_ICON_DIR="${PROJECT_SOURCE_DIR}/KansEditor/Resources/Editor/ContentBrowser"
    KANS_TEST_EDITOR_ASSET_DIR="${PROJECT_SOURCE_DIR}/KansEditor/assets")
kans_copy_test_runtime(GpuTests)
gtest_discover_tests(GpuTests PROPERTIES
    LABELS "Vulkan"
    RUN_SERIAL TRUE
    ENVIRONMENT "VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json;VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json;VK_LOADER_LAYERS_DISABLE=~implicit~")

# Backend construction stays separate from neutral RHI conformance assertions.
add_executable(RHIConformanceTests GpuTestContext.cpp RHIConformanceTests.cpp VulkanSafetyTests.cpp VulkanImGuiTests.cpp)
target_link_libraries(RHIConformanceTests PRIVATE Kans3D GTest::gtest_main)
kans_copy_test_runtime(RHIConformanceTests)
gtest_discover_tests(RHIConformanceTests PROPERTIES
    LABELS "Vulkan"
    RUN_SERIAL TRUE
    ENVIRONMENT "VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json;VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json;VK_LOADER_LAYERS_DISABLE=~implicit~")

add_executable(WindowPresentationTests WindowPresentationTests.cpp)
target_link_libraries(WindowPresentationTests PRIVATE Kans3D GTest::gtest_main)
kans_copy_test_runtime(WindowPresentationTests)
gtest_discover_tests(WindowPresentationTests PROPERTIES
    LABELS "Vulkan" RUN_SERIAL TRUE
    ENVIRONMENT "VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json;VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json;KANS_VULKAN_REQUIRE_CPU=1;VK_LOADER_LAYERS_DISABLE=~implicit~")
