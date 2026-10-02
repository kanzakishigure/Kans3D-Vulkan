find_package(GTest REQUIRED)
add_executable(Kans3DTests
    SourceAssetIDTests.cpp
    AssetIDTests.cpp
    AssetPathTests.cpp
    SourceAssetMetadataTests.cpp
    SourceAssetMetaSerializerTests.cpp
    SourceAssetDatabaseTests.cpp
    AssetScannerTests.cpp
    SourceAssetDatabaseSerializerTests.cpp
    ProjectTests.cpp
    ../KansEditor/src/EditorAssetCachePath.cpp
)
target_link_libraries(Kans3DTests PRIVATE Kans3D GTest::gtest_main)
target_compile_features(Kans3DTests PRIVATE cxx_std_17)
include(GoogleTest)
gtest_discover_tests(Kans3DTests)
