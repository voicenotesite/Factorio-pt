cmake_minimum_required(VERSION 3.20)
project(FactorioPt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FACTORIO_PT_BUILD_RUNTIME "Build C++ runtime executable" ON)

if(FACTORIO_PT_BUILD_RUNTIME)
  add_subdirectory(runtime)
endif()

