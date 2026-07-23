cmake_minimum_required(VERSION 3.20)
project(TestAiNES VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

add_library(nes_core
    src/core/APU.cpp
    src/core/Bus.cpp
    src/core/CPU6502.cpp
    src/core/Cartridge.cpp
    src/core/Nes.cpp
    src/core/PPU.cpp
    src/core/mappers/Mmc1Mapper.cpp
    src/core/mappers/Mmc3Mapper.cpp
    src/core/mappers/NromMapper.cpp
    src/core/mappers/Vrc6Mapper.cpp
)

target_include_directories(nes_core PUBLIC src)

if(MSVC)
    target_compile_options(nes_core PRIVATE /W4)
else()
    target_compile_options(nes_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)

    add_executable(TestAiNES MACOSX_BUNDLE
        src/macos/main.mm
    )
    target_link_libraries(TestAiNES PRIVATE nes_core
        "-framework Cocoa"
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
        "-framework AudioToolbox"
        "-framework CoreAudio"
        "-framework UniformTypeIdentifiers"
    )
    set_target_properties(TestAiNES PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "local.testai.nes"
        MACOSX_BUNDLE_BUNDLE_NAME "TestAiNES"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
        MACOSX_BUNDLE_COPYRIGHT "Copyright 2026"
    )
    target_compile_options(TestAiNES PRIVATE -fobjc-arc)
    add_custom_command(TARGET TestAiNES POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:TestAiNES>/Contents/Resources"
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:TestAiNES>"
        VERBATIM
    )
endif()

if(WIN32)
    add_executable(TestAiNES WIN32
        src/win32/main.cpp
    )
    target_link_libraries(TestAiNES PRIVATE nes_core winmm comdlg32)
endif()

add_executable(nes_core_tests
    tests/CoreTests.cpp
)
target_link_libraries(nes_core_tests PRIVATE nes_core)

enable_testing()
add_test(NAME nes_core_tests COMMAND nes_core_tests)
