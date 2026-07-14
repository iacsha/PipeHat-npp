cmake_minimum_required(VERSION 3.20)
project(PipeHat VERSION 1.0.0 LANGUAGES CXX)

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
    src/resource.rc
)

target_include_directories(PipeHat PRIVATE
    include
    src
)

target_link_libraries(PipeHat PRIVATE
    user32 gdi32 comctl32 comdlg32
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
