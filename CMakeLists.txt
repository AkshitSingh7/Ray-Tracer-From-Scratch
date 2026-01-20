cmake_minimum_required(VERSION 3.11)
project(RayTracer)

set(CMAKE_CXX_STANDARD 17)

# disable audio to prevent build errors on some windows environments
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# download raylib
include(FetchContent)
FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/master.tar.gz
)
FetchContent_MakeAvailable(raylib)

# find openmp for multithreading
find_package(OpenMP REQUIRED)

add_executable(raytracer src/main.cpp)

# link libraries (windows requires gdi32 and winmm)
if (WIN32)
    target_link_libraries(raytracer raylib gdi32 winmm OpenMP::OpenMP_CXX)
else()
    target_link_libraries(raytracer raylib OpenMP::OpenMP_CXX)
endif()
