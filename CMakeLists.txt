set(CMAKE_CXX_STANDARD 20)

project(render)

add_subdirectory(ext/liblava)

add_executable(render main.cpp)

target_link_libraries(render PRIVATE lava::app)
