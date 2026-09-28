# Pattern-routing kernel backends (GPU_BACKEND_PLAN.md).
#   cmake -S gr/gpu -B gr/gpu/build && cmake --build gr/gpu/build
#   gr/gpu/build/pattern_selftest
cmake_minimum_required(VERSION 3.20)
# Build natively on Apple silicon even when cmake itself is an Intel binary
# running under Rosetta (as /usr/local/bin/cmake is here): otherwise every
# target is x86_64, runs translated, and its floating point differs from the
# native one-line g++ build (arm64 fuses multiply-adds) -- which is exactly
# how pd11route_gpu first diverged from pd11route (HISTORY_COST_SWEEP.md
# Part 72). Must be set before project().
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
  execute_process(COMMAND sysctl -n hw.optional.arm64 OUTPUT_VARIABLE GR_ARM64 ERROR_QUIET
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(GR_ARM64 STREQUAL "1")
    set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "Target architecture" FORCE)
  endif()
endif()
project(gr_gpu LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)
# Same optimization as every router's one-line `g++ -std=c++17 -O2 -Wall
# -Wextra` build -- not CMake's Release default (-O3 -DNDEBUG). The router's
# floating-point paths round differently at -O3, so a CMake-built router
# diverged from the g++-built one (HISTORY_COST_SWEEP.md Part 72); with
# identical flags the builds must produce byte-identical routes.
set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_OBJCXX_FLAGS_RELEASE "-O2")

# The Metal source is embedded as a string for the run-time-compile fallback.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/pattern_kernel.metal MSL_SOURCE)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/pattern_kernel_src.inc "R\"MSL(${MSL_SOURCE})MSL\"\n")

set(GR_GPU_SOURCES backend_select.cpp)
set(GR_GPU_DEFS)
set(GR_GPU_LIBS)

if(APPLE)
  option(GR_WITH_METAL "Build the Metal backend" ON)
endif()
if(GR_WITH_METAL)
  enable_language(OBJCXX)
  list(APPEND GR_GPU_SOURCES pattern_metal.mm)
  list(APPEND GR_GPU_DEFS GR_HAVE_METAL)
  list(APPEND GR_GPU_LIBS "-framework Metal" "-framework Foundation")
  set_source_files_properties(pattern_metal.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  # Offline .metallib when the Metal toolchain is installed (Xcode +
  # `xcodebuild -downloadComponent MetalToolchain`); otherwise the run-time
  # compile from the embedded source is used.
  execute_process(COMMAND xcrun -f metal OUTPUT_VARIABLE METAL_BIN ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(METAL_BIN)
    add_custom_command(
      OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pattern_kernel.metallib
      COMMAND xcrun metal -c ${CMAKE_CURRENT_SOURCE_DIR}/pattern_kernel.metal -o ${CMAKE_CURRENT_BINARY_DIR}/pattern_kernel.air
      COMMAND xcrun metallib ${CMAKE_CURRENT_BINARY_DIR}/pattern_kernel.air -o ${CMAKE_CURRENT_BINARY_DIR}/pattern_kernel.metallib
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/pattern_kernel.metal
      COMMENT "Compiling pattern_kernel.metal offline")
    add_custom_target(pattern_metallib ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/pattern_kernel.metallib)
  endif()
endif()

add_library(gr_gpu STATIC ${GR_GPU_SOURCES})
target_include_directories(gr_gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(gr_gpu PUBLIC ${GR_GPU_DEFS})
target_link_libraries(gr_gpu PUBLIC ${GR_GPU_LIBS})

add_executable(pattern_selftest pattern_selftest.cpp)
target_link_libraries(pattern_selftest PRIVATE gr_gpu)

# pd11route with run-time backend selection (GR_GPU=auto|cpu|metal|cuda).
# The plain `g++ pd11route.cpp` build uses the CPU reference only.
add_executable(pd11route_gpu ${CMAKE_CURRENT_SOURCE_DIR}/../pd11route.cpp)
target_compile_definitions(pd11route_gpu PRIVATE GR_PATTERN_SELECT)
target_link_libraries(pd11route_gpu PRIVATE gr_gpu)
