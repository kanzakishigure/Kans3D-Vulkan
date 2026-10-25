find_package(PNG REQUIRED)
add_executable(SceneRender Main.cpp)
target_link_libraries(SceneRender PRIVATE Kans3D PNG::PNG)
kans_set_output_dirs(SceneRender)
