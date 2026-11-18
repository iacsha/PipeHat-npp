cmake_minimum_required(VERSION 3.20)
# The version is set in src/Version.h and nowhere else (see the note there).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h" _pipehat_ver
     REGEX "^#define PIPEHAT_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
foreach(_line IN LISTS _pipehat_ver)
    string(REGEX MATCH "(MAJOR|MINOR|PATCH) ([0-9]+)" _m "${_line}")
    set(_pipehat_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
endforeach()
if(NOT DEFINED _pipehat_MAJOR OR NOT DEFINED _pipehat_MINOR OR NOT DEFINED _pipehat_PATCH)
    message(FATAL_ERROR "Could not read the version from src/Version.h")
endif()
project(PipeHat VERSION ${_pipehat_MAJOR}.${_pipehat_MINOR}.${_pipehat_PATCH} LANGUAGES CXX)
message(STATUS "PipeHat version ${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Notepad++ is Windows-only and always Unicode
add_definitions(-DUNICODE -D_UNICODE)

# Plugin DLL
add_library(PipeHat SHARED
    src/main.cpp
    src/HL7Lexer.cpp
    src/ScintillaStyler.cpp
    src/SegmentDB.cpp
    src/MessageTreeView.cpp
    src/PHIScrubber.cpp
    src/SettingsDialog.cpp
    src/MllpTransport.cpp
    src/UpdateCheck.cpp
    src/resource.rc
)

target_include_directories(PipeHat PRIVATE
    include
    src
)

target_link_libraries(PipeHat PRIVATE
    user32 gdi32 comctl32 comdlg32 ws2_32 winhttp shell32
)

set_target_properties(PipeHat PROPERTIES
    PREFIX ""
    SUFFIX ".dll"
)

# Security hardening (defense-in-depth for a DLL parsing untrusted input)
if (MSVC)
    target_compile_options(PipeHat PRIVATE /guard:cf /sdl /GS)
    target_link_options(PipeHat PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT)
endif()
