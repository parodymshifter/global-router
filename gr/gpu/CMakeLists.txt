# Pattern-routing kernel backends (GPU_BACKEND_PLAN.md).
#   cmake -S gr/gpu -B gr/gpu/build && cmake --build gr/gpu/build
#   gr/gpu/build/pattern_selftest
cmake_minimum_required(VERSION 3.20)
project(gr_gpu LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

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
