# castlevania_harmony_of_despair - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(castlevania_harmony_of_despair LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(CASTLEVANIAHARMONYOFDESPAIR_SOURCES
    src/main.cpp
    src/chod_loose_content.cpp
    src/chod_title_exit.cpp
)

if(WIN32)
    add_executable(castlevania_harmony_of_despair WIN32 ${CASTLEVANIAHARMONYOFDESPAIR_SOURCES})
else()
    add_executable(castlevania_harmony_of_despair ${CASTLEVANIAHARMONYOFDESPAIR_SOURCES})
endif()

rexglue_setup_target(castlevania_harmony_of_despair GPU_PLUGINS xenos)
