# Desktop build: the DSP plus a WAV harness, tests, and the pedal simulator.
# The pedal firmware builds separately with firmware/Makefile.
cmake_minimum_required(VERSION 3.20)
project(daisy_bass C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Single precision everywhere, like the Daisy's FPU; warn loudly (our code only).
set(BASS_WARNINGS -Wall -Wextra -Wpedantic -Wdouble-promotion)

# libDaisy's OLED fonts, so desktop screens use the pedal's exact glyphs.
set(LIBDAISY_SRC ${CMAKE_SOURCE_DIR}/lib/libDaisy/src)
add_library(oled_fonts STATIC ${LIBDAISY_SRC}/util/oled_fonts.c)
target_include_directories(oled_fonts PUBLIC ${LIBDAISY_SRC})

add_executable(bassfx desktop/main.cpp)
target_compile_options(bassfx PRIVATE ${BASS_WARNINGS})
# The firmware compiles as C++14 (libDaisy's setting). Building bassfx the
# same way catches C++17-only code in dsp/ before it reaches the pedal.
set_target_properties(bassfx PROPERTIES CXX_STANDARD 14)

add_executable(dsp_tests tests/main.cpp tests/dsp_tests.cpp tests/tuner_tests.cpp tests/control_tests.cpp
                         tests/pedal_tests.cpp)
target_compile_options(dsp_tests PRIVATE ${BASS_WARNINGS})
target_link_libraries(dsp_tests PRIVATE oled_fonts)

enable_testing()
add_test(NAME dsp_tests COMMAND dsp_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# --- pedalsim: the pedal on your Mac -------------------------------------------
# Fetches SDL2 and Dear ImGui on first configure (needs network once).
# Turn off with -DBASS_SIM=OFF to build only bassfx and the tests.
option(BASS_SIM "Build the pedal simulator (fetches SDL2 and Dear ImGui)" ON)
if(BASS_SIM)
  include(FetchContent)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5) # SDL2's CMakeLists predates CMake 4
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL2 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                            GIT_TAG release-2.32.8 GIT_SHALLOW TRUE)
  FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git
                             GIT_TAG v1.91.9b GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(SDL2 imgui)

  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp)
  target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC SDL2::SDL2-static)

  add_executable(pedalsim sim/main.cpp)
  target_compile_options(pedalsim PRIVATE ${BASS_WARNINGS})
  target_link_libraries(pedalsim PRIVATE imgui oled_fonts)
endif()
