cmake_minimum_required(VERSION 3.21)
project(FactorioPtRuntime)

add_executable(factorio_pt_runtime
  src/main.cpp
  src/world.cpp
  src/gameplay.cpp
  src/render.cpp
)
set_target_properties(factorio_pt_runtime PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_compile_definitions(factorio_pt_runtime PRIVATE FACTORIO_PT_APP_NAME="Orbitum")

# Debug (-O0) is too slow for our per-pixel software renderer. Keep debug symbols,
# but enable optimizations for the runtime so it's playable.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if (MSVC)
    target_compile_options(factorio_pt_runtime PRIVATE /O2)
  else()
    target_compile_options(factorio_pt_runtime PRIVATE -O2)
  endif()
endif()

if(WIN32)
  target_link_libraries(factorio_pt_runtime PRIVATE user32 gdi32)
endif()

