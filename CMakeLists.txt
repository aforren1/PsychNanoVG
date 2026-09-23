cmake_minimum_required(VERSION 3.16)

# MATLAB links the MEX with -mmacosx-version-min=11.0, so the static library
# has to target the same version. Without this the linker warns that the
# objects were built for a newer macOS than it is linking for. It has to be
# set before project().
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
        "Oldest macOS the library runs on, matched to MATLAB's mex" FORCE)
endif()

project(psychnanovg C)

# The MEX itself is compiled by `mex`, not here. CMake only builds the static
# library that the MEX links: NanoVG, glad, the null renderer, the GL backend,
# and the core layer. Keeping the core in the library lets the native smoke
# test link the same code without MATLAB.

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(PSYCHNANOVG_TRACY "Compile the Tracy client into the library" OFF)
option(PSYCHNANOVG_SMOKE_GL "Build the native GL smoke test" OFF)
set(PSYCHNANOVG_STATS 1 CACHE STRING "1 keeps the Stats counters, 0 removes them")

set(NANOVG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/nanovg)
if(NOT EXISTS ${NANOVG_DIR}/src/nanovg.c)
    message(FATAL_ERROR
        "NanoVG is missing. Run:\n"
        "  git clone https://github.com/memononen/nanovg.git third_party/nanovg\n"
        "See third_party/PINS.md for the pinned commit.")
endif()

add_library(pnvg STATIC
    src/core/pnvg_nanovg_unit.c
    src/core/pnvg_core.c
    src/core/pnvg_null.c
    src/pnvg_gl.c
    third_party/glad/src/gl.c
)

target_include_directories(pnvg PUBLIC
    ${NANOVG_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(pnvg PUBLIC PSYCHNANOVG_STATS=${PSYCHNANOVG_STATS})

# SPEC 4.1: Psychtoolbox contexts are GL 2.1 on macOS, so the GL2 backend is
# the only one that works there.
if(APPLE)
    # OpenGL is deprecated on macOS but still present, and it is what
    # Psychtoolbox uses. The define silences the deprecation warning on
    # every OpenGL and CGL call so a real warning stays visible.
    target_compile_definitions(pnvg PUBLIC PNVG_GL2=1 GL_SILENCE_DEPRECATION=1)
endif()

if(PSYCHNANOVG_TRACY)
    set(TRACY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tracy)
    if(NOT EXISTS ${TRACY_DIR}/public/TracyClient.cpp)
        message(FATAL_ERROR
            "PSYCHNANOVG_TRACY is ON but Tracy is missing: "
            "${TRACY_DIR}/public/TracyClient.cpp does not exist. Run:\n"
            "  git clone --branch v0.11.1 https://github.com/wolfpld/tracy.git third_party/tracy\n"
            "or configure with -DPSYCHNANOVG_TRACY=OFF. See third_party/PINS.md.")
    endif()
    enable_language(CXX)
    target_sources(pnvg PRIVATE
        ${TRACY_DIR}/public/TracyClient.cpp
        src/core/pnvg_tracy.cpp)
    target_include_directories(pnvg PUBLIC ${TRACY_DIR}/public)
    # A MEX file is loaded and unloaded inside a long-lived process, so the
    # profiler starts on the first call and stops in mexAtExit instead of in
    # static constructors. The host also owns crash handling: MATLAB's JVM
    # raises access violations on purpose and handles them, and Tracy's
    # handler took the first one for a crash, suspended every other thread,
    # and hung MATLAB in Screen('CloseAll'). build.m passes the same defines
    # to mex.
    target_compile_definitions(pnvg PUBLIC PSYCHNANOVG_TRACY=1 TRACY_ENABLE
        TRACY_DELAYED_INIT TRACY_MANUAL_LIFETIME TRACY_NO_CRASH_HANDLER)
    if(WIN32)
        target_link_libraries(pnvg PUBLIC ws2_32 dbghelp advapi32 user32)
    elseif(NOT APPLE)
        target_link_libraries(pnvg PUBLIC pthread)
    endif()
endif()

if(MSVC)
    # NanoVG and stb use the classic C runtime string functions on purpose.
    target_compile_definitions(pnvg PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(pnvg PRIVATE -Wall -Wno-unused-function)
endif()

if(WIN32)
    target_link_libraries(pnvg PUBLIC opengl32)
elseif(APPLE)
    target_link_libraries(pnvg PUBLIC "-framework OpenGL")
else()
    target_link_libraries(pnvg PUBLIC GL dl m)
endif()

install(TARGETS pnvg ARCHIVE DESTINATION lib)

# ---------------------------------------------------------------------------
# Native smoke test
# ---------------------------------------------------------------------------
# Psychtoolbox is not installed on every developer machine, and tests/gl/ needs
# it. This executable opens its own hidden window and context so the GL path
# still gets exercised before a PTB machine is available.

if(PSYCHNANOVG_SMOKE_GL)
    add_executable(smoke_gl tests/smoke_gl.c)
    target_link_libraries(smoke_gl PRIVATE pnvg)
    if(WIN32)
        target_link_libraries(smoke_gl PRIVATE gdi32 user32)
    elseif(NOT APPLE)
        # The test opens its own GLX window, so it needs Xlib directly.
        target_link_libraries(smoke_gl PRIVATE X11)
    endif()
    install(TARGETS smoke_gl RUNTIME DESTINATION bin)
endif()
