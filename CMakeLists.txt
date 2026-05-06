cmake_minimum_required(VERSION 3.20)

project(hyprmural
    VERSION 0.0.0
    DESCRIPTION "Per-workspace wallpaper tool for Hyprland"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_EGL REQUIRED IMPORTED_TARGET wayland-egl)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GLES REQUIRED IMPORTED_TARGET glesv2)
pkg_check_modules(WEBP REQUIRED IMPORTED_TARGET libwebp)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)

set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GENERATED_DIR})

function(wayland_protocol XML BASE)
    add_custom_command(
        OUTPUT ${GENERATED_DIR}/${BASE}-protocol.c
        COMMAND ${WAYLAND_SCANNER} private-code ${XML} ${GENERATED_DIR}/${BASE}-protocol.c
        DEPENDS ${XML}
        VERBATIM)
    add_custom_command(
        OUTPUT ${GENERATED_DIR}/${BASE}-client-protocol.h
        COMMAND ${WAYLAND_SCANNER} client-header ${XML} ${GENERATED_DIR}/${BASE}-client-protocol.h
        # `namespace` is a C++ keyword but wayland-scanner emits it as a
        # parameter name in wlr-layer-shell. Rename to keep the header
        # includable from C++.
        COMMAND sed -i "s/\\*namespace/*_namespace/g" ${GENERATED_DIR}/${BASE}-client-protocol.h
        COMMAND sed -i "s/, namespace/, _namespace/g" ${GENERATED_DIR}/${BASE}-client-protocol.h
        DEPENDS ${XML}
        VERBATIM)
endfunction()

wayland_protocol(${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml xdg-shell)
wayland_protocol(${CMAKE_CURRENT_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml
                 wlr-layer-shell-unstable-v1)

add_executable(hyprmural
    src/main.cpp
    src/wayland.cpp
    src/egl.cpp
    src/image.cpp
    src/renderer.cpp
    src/layer_surface.cpp
    src/config.cpp
    src/hyprland_ipc.cpp
    src/hooks.cpp
    ${GENERATED_DIR}/xdg-shell-protocol.c
    ${GENERATED_DIR}/xdg-shell-client-protocol.h
    ${GENERATED_DIR}/wlr-layer-shell-unstable-v1-protocol.c
    ${GENERATED_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h)

target_include_directories(hyprmural PRIVATE
    ${GENERATED_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

target_link_libraries(hyprmural PRIVATE
    PkgConfig::WAYLAND_CLIENT
    PkgConfig::WAYLAND_EGL
    PkgConfig::EGL
    PkgConfig::GLES
    PkgConfig::WEBP)
