cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)

add_subdirectory(ext/liblava)

# TODO: This can be greatly simplified.

project(sorting)
add_executable(sorting src/sorting.cpp)
if(WIN32)
  target_compile_definitions(sorting PRIVATE WIN32)
endif()
target_compile_definitions(sorting PRIVATE Release=$<CONFIG:Release>)
target_link_libraries(sorting PRIVATE lava::app)
target_sources(sorting PRIVATE
  src/shared_behavior.hpp)

project(reference)
add_executable(reference src/reference.cpp)
if(WIN32)
  target_compile_definitions(reference PRIVATE WIN32)
endif()
target_compile_definitions(reference PRIVATE Release=$<CONFIG:Release>)
target_link_libraries(reference PRIVATE lava::app)
target_sources(reference PRIVATE
  src/shared_behavior.hpp)

project(alternative_short)
add_executable( alternative_short src/alternative.cpp)
if(WIN32)
  target_compile_definitions(alternative_short PRIVATE WIN32)
endif()
target_compile_definitions(alternative_short PRIVATE Release=$<CONFIG:Release>)
target_link_libraries(alternative_short PRIVATE lava::app)
target_sources(alternative_short PRIVATE
  src/shared_behavior.hpp)

project(alternative_int)
add_executable(alternative_int src/alternative.cpp)
if(WIN32)
  target_compile_definitions(alternative_int PRIVATE WIN32)
endif()
target_compile_definitions(alternative_int PRIVATE Release=$<CONFIG:Release>)
target_link_libraries(alternative_int PRIVATE lava::app)
target_sources(alternative_int PRIVATE
  src/shared_behavior.hpp)
