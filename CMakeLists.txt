
#  Copyright (c) 2020 Damjan Marion
#
#  Permission is hereby granted, free of charge, to any person obtaining
#  a copy of this software and associated documentation files (the
#  "Software"), to deal in the Software without restriction, including
#  without limitation the rights to use, copy, modify, merge, publish,
#  distribute, sublicense, and/or sell copies of the Software, and to
#  permit persons to whom the Software is furnished to do so, subject to
#  the following conditions:
#
#  The above copyright notice and this permission notice shall be
#  included in all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
#  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
#  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
#  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
#  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
#  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
#  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

cmake_minimum_required(VERSION 3.5)

project(vpp-toys)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
find_path(VPP_RELEASE_INSTALL_PATH
	  NAMES include/vppinfra/clib.h
	  PATHS
	    ${VPP_PATH}/build-root/install-vpp-native/vpp
	    ${CMAKE_SOURCE_DIR}/../vpp/build-root/install-vpp-native/vpp )

if(NOT VPP_RELEASE_INSTALL_PATH)
  message(FATAL_ERROR "Cannot find VPP tree. Try to specify -DVPP_PATH:PATH=</path/to/vpp/dir>")
endif ()

message(STATUS "Using VPP tree: ${VPP_RELEASE_INSTALL_PATH}")

set(CMAKE_C_FLAGS "-g -fstack-protector -fno-common -Wall -Werror")

list(APPEND MARCH_VARIANTS "sse42\;-march=corei7 -mtune=corei7-avx")
list(APPEND MARCH_VARIANTS "avx2\;-march=core-avx2 -mtune=core-avx2")
list(APPEND MARCH_VARIANTS "avx512\;-march=skylake-avx512")

find_library (VPPINFRA_LIB
  NAMES "vppinfra"
  PATHS ${VPP_RELEASE_INSTALL_PATH}/lib
  NO_DEFAULT_PATH
)

macro(add_exec exec)
  cmake_parse_arguments(ARG "VARIANTS" "" "SOURCES" ${ARGN})

  if (ARG_VARIANTS)
    foreach(V ${MARCH_VARIANTS})
      list(GET V 0 VARIANT)
      list(GET V 1 VARIANT_FLAGS)
      set(e ${exec}.${VARIANT})
      add_executable(${e} ${ARG_SOURCES})
      target_link_libraries(${e} ${VPPINFRA_LIB})
      target_include_directories(${e} PUBLIC ${VPP_RELEASE_INSTALL_PATH}/include)
      separate_arguments(VARIANT_FLAGS)
      target_compile_options(${e} PUBLIC ${VARIANT_FLAGS} -O3)
    endforeach()
  else()
    add_executable(${exec} ${ARG_SOURCES})
    target_link_libraries(${exec} ${VPPINFRA_LIB})
    target_include_directories(${exec} PUBLIC ${VPP_RELEASE_INSTALL_PATH}/include)
    target_compile_options(${exec} PUBLIC -march=native -O3)
  endif()
  # Debug
  set(e ${exec}.debug)
  add_executable(${e} ${ARG_SOURCES})
  target_link_libraries(${e} ${VPPINFRA_LIB})
  target_include_directories(${e} PUBLIC ${VPP_RELEASE_INSTALL_PATH}/include)
  target_compile_options(${e} PUBLIC -march=native -O0)
endmacro()

add_exec(hash_ip_lookup_perf SOURCES src/hash_ip_lookup_perf.c VARIANTS)
add_exec(perf_store_forwarding SOURCES src/perf_store_forwarding.c)

