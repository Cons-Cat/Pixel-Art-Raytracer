#include <liblava/lava.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits.h>
#include <limits>
#include <string>

// TODO: Automate this in CMake.
#if Release == 1
#define SHADERS_PATH "./res/"
#else
#define SHADERS_PATH "../../res/"
#endif

#ifdef WIN32
#include <windows.h>
auto get_run_path() -> std::string {
    std::array<char, MAX_PATH> result{};
    std::string full_path = std::string(
        result.data(), GetModuleFileName(NULL, result.data(), MAX_PATH));
    return std::string(full_path.substr(0, full_path.find_last_of('\\\\'))) +
           "/";
}
#else
#include <linux/limits.h>
#include <unistd.h>
auto get_run_path() -> std::string {
    std::array<char, PATH_MAX - NAME_MAX> result{};
    ssize_t count =
        readlink("/proc/self/exe", result.data(), PATH_MAX - NAME_MAX);
    std::string full_path = std::string(result.data());
    return std::string(full_path.substr(0, full_path.find_last_of('/'))) + "/";
}
#endif

uint32_t view_width = 480u;
uint32_t view_height = 300u;

struct PointLight {
    alignas(16) std::array<int32_t, 3> position;
};
