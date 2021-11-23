cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)

add_subdirectory(ext/liblava)

project(sorting)
add_executable(sorting src/sorting.cpp)
if (WIN32)
  target_compile_definitions(sorting PRIVATE WIN32)
endif()
target_compile_definitions(sorting PRIVATE Release=$<CONFIG:Release>)
target_link_libraries(sorting PRIVATE lava::app)
target_sources(
  sorting PRIVATE
  src/shared_behavior.hpp
  )
