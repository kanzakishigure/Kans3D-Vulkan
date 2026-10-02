find_package(GTest REQUIRED)
add_executable(Kans3DTests
    ModelLoadingTests.cpp
    MeshUtilsTests.cpp
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
