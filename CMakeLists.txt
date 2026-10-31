# Desktop build only: the DSP plus a WAV harness and tests.
# The pedal firmware builds separately with firmware/Makefile.
cmake_minimum_required(VERSION 3.20)
project(daisy_bass CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Single precision everywhere, like the Daisy's FPU; warn loudly.
add_compile_options(-Wall -Wextra -Wpedantic -Wdouble-promotion)

add_executable(bassfx desktop/main.cpp)
add_executable(dsp_tests tests/dsp_tests.cpp)

enable_testing()
add_test(NAME dsp_tests COMMAND dsp_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
