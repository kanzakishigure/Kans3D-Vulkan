find_package(GTest REQUIRED)
add_executable(Kans3DTests
    ModelLoadingTests.cpp
    MeshUtilsTests.cpp
    ContentBrowserLayoutTests.cpp
)
target_link_libraries(Kans3DTests PRIVATE Kans3D GTest::gtest_main)
target_compile_features(Kans3DTests PRIVATE cxx_std_17)
target_compile_definitions(Kans3DTests PRIVATE
    KANS_TEST_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../KansEditor/assets/model")
include(GoogleTest)
gtest_discover_tests(Kans3DTests)

add_executable(SmoothNormalsBenchmark SmoothNormalsBenchmark.cpp)
target_link_libraries(SmoothNormalsBenchmark PRIVATE Kans3D)
add_executable(ModelLoadBenchmark ModelLoadBenchmark.cpp)
target_link_libraries(ModelLoadBenchmark PRIVATE Kans3D)
target_compile_definitions(ModelLoadBenchmark PRIVATE KANS_PROJECT_ROOT="${PROJECT_SOURCE_DIR}")

# Real GL tests run separately from the CPU-only suite.
add_executable(OpenGLTextureTests OpenGLTextureTests.cpp)
target_link_libraries(OpenGLTextureTests PRIVATE Kans3D GTest::gtest_main)
target_compile_definitions(OpenGLTextureTests PRIVATE
    KANS_TEST_ICON_DIR="${PROJECT_SOURCE_DIR}/KansEditor/Resources/Editor/ContentBrowser")
if(UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED COMPONENTS EGL)
    target_link_libraries(OpenGLTextureTests PRIVATE OpenGL::EGL)
    target_compile_definitions(OpenGLTextureTests PRIVATE KANS_TEST_USE_EGL)
endif()
gtest_discover_tests(OpenGLTextureTests PROPERTIES LABELS "OpenGL")
if(UNIX AND NOT APPLE)
    # Mesa otherwise returns its highest supported core version, masking the
    # editor's GL 4.3 + ARB_direct_state_access loader path.
    add_test(NAME OpenGLTextureMipmap.GL43ExtensionPath COMMAND OpenGLTextureTests)
    set_tests_properties(OpenGLTextureMipmap.GL43ExtensionPath PROPERTIES
        ENVIRONMENT "MESA_GL_VERSION_OVERRIDE=4.3;LIBGL_ALWAYS_SOFTWARE=1"
        LABELS "OpenGL")
endif()
