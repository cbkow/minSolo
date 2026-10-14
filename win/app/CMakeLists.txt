cmake_minimum_required(VERSION 3.21)
project(minSolo VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: one exe with no VC++ redistributable to install (Inno) or bundle (MSIX).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

configure_file(res/version.h.in "${CMAKE_BINARY_DIR}/version.h" @ONLY)

add_executable(minSolo WIN32
    src/main.cpp
    src/sessions.cpp
    src/engine.cpp
    src/flyout.cpp
    src/log.cpp
    res/minSolo.rc)
target_include_directories(minSolo PRIVATE src "${CMAKE_BINARY_DIR}" res)
target_compile_definitions(minSolo PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(minSolo PRIVATE /W4 /EHsc /utf-8)
target_link_libraries(minSolo PRIVATE ole32 shell32 user32 gdi32 advapi32 version shlwapi dwmapi)
set_target_properties(minSolo PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME minSolo)
# The manifest is embedded by the linker from the .rc (RT_MANIFEST); stop CMake adding a default one.
target_link_options(minSolo PRIVATE /MANIFEST:NO)

# ---- MSIX (Microsoft Store / signed sideload) ----
set(MINSOLO_MSIX_IDENTITY               "cbkow.minSolo"
    CACHE STRING "MSIX Identity/Name - must match the name reserved in Microsoft Partner Center.")
set(MINSOLO_MSIX_PUBLISHER              "CN=C64A1D41-EFAA-4EED-AE81-0C074867D4E9"
    CACHE STRING "MSIX Identity/Publisher - the publisher CN registered with Partner Center (and the sideload cert subject).")
set(MINSOLO_MSIX_PUBLISHER_DISPLAY_NAME "cbkow"
    CACHE STRING "Publisher name shown in the Store / Settings > Apps.")
set(MINSOLO_MSIX_VERSION "${PROJECT_VERSION}.0")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/msix/AppxManifest.xml.in" "${CMAKE_BINARY_DIR}/msix/AppxManifest.xml" @ONLY)
find_program(MINSOLO_PWSH NAMES pwsh powershell)
if(MINSOLO_PWSH)
    add_custom_target(minsolo_msix
        COMMAND "${MINSOLO_PWSH}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/msix/build_msix.ps1"
            -BuildDir     "$<TARGET_FILE_DIR:minSolo>"
            -Version      "${PROJECT_VERSION}"
            -ManifestPath "${CMAKE_BINARY_DIR}/msix/AppxManifest.xml"
            -ImagesDir    "${CMAKE_CURRENT_SOURCE_DIR}/msix/images"
            -OutputDir    "${CMAKE_CURRENT_SOURCE_DIR}/../../dist"
        DEPENDS minSolo
        COMMENT "Packaging minSolo ${PROJECT_VERSION} as MSIX (output: dist/)"
        VERBATIM)
endif()
