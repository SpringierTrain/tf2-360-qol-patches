cmake_minimum_required(VERSION 3.16)
project(tf2_360_qol_patches CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime, so release builds run without the VC++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(tf2_360_qol_patches src/main.cpp src/patch_deathnotice.cpp src/patch_ubercharge.cpp)
