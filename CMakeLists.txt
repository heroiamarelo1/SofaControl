# SofaControl - CMake build (Visual Studio / Build Tools)
cmake_minimum_required(VERSION 3.20)

project(SofaControl VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Generate the application icon (assets/SofaControl.ico) from assets/app_icon.png.
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/app_icon.png" AND NOT EXISTS "${CMAKE_SOURCE_DIR}/assets/SofaControl.ico")
    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/generate_icon.ps1"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )
endif()

# ViGEmClient (vendored by build.ps1) powers the Emulated Xbox 360 backend.
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/ViGEmClient/CMakeLists.txt")
    add_subdirectory(third_party/ViGEmClient)
endif()

add_executable(SofaControl WIN32
    src/main.cpp
    src/SofaControl.rc
    src/ControllerWindow.cpp
    src/XboxController.cpp
    src/MouseInput.cpp
    src/ModeManager.cpp
    src/AppSettings.cpp
    src/AppConfig.cpp
    src/SystemActions.cpp
    src/DeviceHider.cpp
    src/HidHidePaths.cpp
    src/VirtualGamepad.cpp
    src/IntegratedKeyboard.cpp
    src/SearchOverlay.cpp
    src/KeyboardInput.cpp
    src/AltTabSwitcher.cpp
    src/ControllerShortcuts.cpp
    src/WindowManager.cpp
)

add_executable(SofaControlGuard
    src/GuardService.cpp
    src/DeviceHider.cpp
    src/HidHidePaths.cpp
)

target_link_libraries(SofaControl PRIVATE
    ViGEmClient::ViGEmClient
    xinput
    user32
    gdi32
    comctl32
    winmm
    setupapi
    cfgmgr32
    hid
    powrprof
    shell32
    advapi32
    gdiplus
)

target_link_libraries(SofaControlGuard PRIVATE
    xinput
    setupapi
    cfgmgr32
    hid
    advapi32
    shell32
)

target_compile_definitions(SofaControl PRIVATE UNICODE _UNICODE)
target_compile_definitions(SofaControlGuard PRIVATE UNICODE _UNICODE)

if(MSVC)
    # /wd4828 silences UTF-8 byte warnings from the vendored ViGEmClient header.
    target_compile_options(SofaControl PRIVATE /utf-8 /wd4828)
    target_compile_options(SofaControlGuard PRIVATE /utf-8)
endif()

# Copy toggle.wav into the output folder
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/assets/toggle.wav")
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/generate_toggle_wav.py"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
    else()
        execute_process(
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/generate_toggle_wav.ps1"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
    endif()
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/assets/toggle.wav")
    add_custom_command(TARGET SofaControl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SofaControl>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/toggle.wav"
            "$<TARGET_FILE_DIR:SofaControl>/assets/toggle.wav"
        COMMENT "Copying assets/toggle.wav"
    )
endif()

# Copy the Command List infographic into the output folder.
if(EXISTS "${CMAKE_SOURCE_DIR}/assets/command_list.png")
    add_custom_command(TARGET SofaControl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:SofaControl>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/command_list.png"
            "$<TARGET_FILE_DIR:SofaControl>/assets/command_list.png"
        COMMENT "Copying assets/command_list.png"
    )
endif()
