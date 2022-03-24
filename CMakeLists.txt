cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

project(alternative)
add_executable(alternative src/alternative.cpp)
target_compile_definitions(alternative PRIVATE Release=$<CONFIG:Release>)
target_link_libraries(alternative PRIVATE ${SDL2_LIBRARIES})
target_sources(alternative PRIVATE
  src/sprites.hpp)
