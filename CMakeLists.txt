set(CMAKE_CXX_STANDARD 20)

project(render)

add_subdirectory(ext/liblava)

add_executable(render main.cpp)

if (WIN32)
  target_compile_definitions(render PRIVATE WIN32)
endif()

target_link_libraries(render PRIVATE lava::app)
