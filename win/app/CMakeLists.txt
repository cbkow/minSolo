cmake_minimum_required(VERSION 3.21)
project(minSolo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

configure_file(res/version.h.in "${CMAKE_BINARY_DIR}/version.h" @ONLY)

add_executable(minSolo WIN32
    src/main.cpp
    src/sessions.cpp
    src/engine.cpp
    src/log.cpp
    res/minSolo.rc)
target_include_directories(minSolo PRIVATE src "${CMAKE_BINARY_DIR}" res)
target_compile_definitions(minSolo PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(minSolo PRIVATE /W4 /EHsc /utf-8)
target_link_libraries(minSolo PRIVATE ole32 shell32 user32 advapi32 version shlwapi)
set_target_properties(minSolo PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME minSolo)
# The manifest is embedded by the linker from the .rc (RT_MANIFEST); stop CMake adding a default one.
target_link_options(minSolo PRIVATE /MANIFEST:NO)
