cmake_minimum_required(VERSION 3.20)

project(hyprmural
    VERSION 0.0.0
    DESCRIPTION "Per-workspace wallpaper tool for Hyprland"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)

add_executable(hyprmural
    src/main.cpp
    src/wayland.cpp)

target_link_libraries(hyprmural PRIVATE PkgConfig::WAYLAND_CLIENT)
