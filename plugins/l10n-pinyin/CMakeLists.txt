cmake_minimum_required(VERSION 3.20)
project(fuzzel-plugin-pinyin)

set(CMAKE_CXX_STANDARD 20)
set(cpp-pinyin-root ${CMAKE_CURRENT_SOURCE_DIR}/cpp-pinyin)

file(GLOB_RECURSE sources src/*.cpp ${cpp-pinyin-root}/include/*
     ${cpp-pinyin-root}/src/*)

include_directories(${cpp-pinyin-root}/include ${cpp-pinyin-root}/src
                    ${cpp-pinyin-root}/src/toneUtil)

add_library(fuzzel-plugin-pinyin SHARED ${sources})

option(ENABLE_TEST "docstring" ON)
if(ENABLE_TEST)
  add_executable(test test/main.c)
endif()

install(TARGETS fuzzel-plugin-pinyin
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/share/fuzzel/)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/cpp-pinyin/res/${dict}
        DESTINATION ${CMAKE_INSTALL_PREFIX}/share/fuzzel/)
