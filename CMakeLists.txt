cmake_minimum_required(VERSION 3.13)
project(f00sel C)

set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

set_property(DIRECTORY . APPEND PROPERTY COMPILE_DEFINITIONS
  $<$<CONFIG:Debug>:_DEBUG>
  _GNU_SOURCE
  )

set(CMAKE_C_FLAGS "-Wall -Werror ${CMAKE_C_FLAGS}")

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(fontconfig REQUIRED IMPORTED_TARGET fontconfig)
pkg_check_modules(cairo REQUIRED IMPORTED_TARGET cairo cairo-ft)
pkg_check_modules(wayland REQUIRED IMPORTED_TARGET
  wayland-client wayland-cursor wlroots)
pkg_check_modules(xkb REQUIRED IMPORTED_TARGET xkbcommon)

find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner)
if (WAYLAND_SCANNER_EXECUTABLE-NOTFOUND)
  message(FATAL_ERROR "could not find wayland-scanner")
endif ()

function (wayland_protocol _deps)
  set(deps "")
  foreach (xml_file ${ARGN})
    get_filename_component(base ${xml_file} NAME_WE)
    set(out_c ${base}.c)
    set(out_h ${base}.h)

    add_custom_command(
      OUTPUT ${out_h}
      COMMAND ${WAYLAND_SCANNER_EXECUTABLE} client-header < ${xml_file} > ${out_h}
      VERBATIM
      MAIN_DEPENDENCY ${xml_file}
      )
    add_custom_command(
      OUTPUT ${out_c}
      COMMAND ${WAYLAND_SCANNER_EXECUTABLE} private-code < ${xml_file} > ${out_c}
      VERBATIM
      MAIN_DEPENDENCY ${xml_file}
      )

    list(APPEND deps ${out_h})
    list(APPEND deps ${out_c})
  endforeach ()

  set(${_deps} ${deps} PARENT_SCOPE)
endfunction ()

pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
execute_process(
  COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=pkgdatadir wayland-protocols
  OUTPUT_VARIABLE WAYLAND_PROTOCOLS
  OUTPUT_STRIP_TRAILING_WHITESPACE)

wayland_protocol(
  wayland_protos
  ${PROJECT_SOURCE_DIR}/external/wlr-protocols/unstable/wlr-layer-shell-unstable-v1.xml
  ${WAYLAND_PROTOCOLS}/stable/xdg-shell/xdg-shell.xml
  ${WAYLAND_PROTOCOLS}/unstable/xdg-output/xdg-output-unstable-v1.xml
  )

add_library(wayland-protocols STATIC EXCLUDE_FROM_ALL ${wayland_protos})
target_include_directories(wayland-protocols PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

add_executable(f00sel
  main.c
  font.c font.h
  render.c render.h
  shm.c shm.h
  log.c log.h
  xdg.c xdg.h
  tllist.h
  )
target_link_libraries(f00sel
  wayland-protocols
  ${CMAKE_THREAD_LIBS_INIT}
  PkgConfig::wayland
  PkgConfig::cairo
  PkgConfig::fontconfig
  PkgConfig::xkb)
