cmake_minimum_required(VERSION 3.20)

project(SSMTPlayerTweaks LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/MinHook
    ${CMAKE_CURRENT_BINARY_DIR}/MinHook
)

add_library(SSMT-Player-Tweaks SHARED
    dllMain.cpp

    Core/Bootstrap.cpp
    Core/Bootstrap.h

    Core/HookManager.cpp
    Core/HookManager.h

    Core/PatternScanner.cpp
    Core/PatternScanner.h

    Core/Pattern.h
    Core/CameraPolicy.cpp
    Core/CameraPolicy.h
    Core/SymbolResolver.cpp
    Core/SymbolResolver.h

    Games/Genshin/GenshinPatterns.cpp
    Games/Genshin/GenshinPatterns.h

    Games/Genshin/Features/CameraTweaks/CameraTweaks.cpp
    Games/Genshin/Features/CameraTweaks/CameraTweaks.h
    Games/Genshin/Features/CameraTweaks/CameraZoom.h
    Games/Genshin/Features/CameraTweaks/CameraZoom.cpp
    Games/Genshin/GenshinCameraBridge.cpp
    Games/Genshin/GenshinCameraBridge.h
    Games/Genshin/GenshinConfig.cpp

    Games/Genshin/Features/FpsUnlock/FpsUnlock.h
    Games/Genshin/Features/FpsUnlock/FpsUnlock.cpp
    Games/Genshin/Features/FpsUnlock/FpsUnlockConfig.h

    Games/GameRuntime.h
    Games/GameRuntime.cpp

    Games/Genshin/GenshinRunTime.h
    Games/Genshin/GenshinRunTime.cpp
    Games/Genshin/GenshinConfig.h
    Games/Genshin/Features/FastTeamPage/FastTeamPage.h
    Games/Genshin/Features/FastTeamPage/FastTeamPage.cpp
    Games/Genshin/Features/FastTeamPage/FastTeamPageConfig.h
)

target_include_directories(SSMT-Player-Tweaks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Core
    ${CMAKE_CURRENT_SOURCE_DIR}/Games/Genshin
)

target_link_libraries(SSMT-Player-Tweaks PRIVATE
    minhook
)

if(MSVC)
    target_compile_options(SSMT-Player-Tweaks PRIVATE /utf-8)
endif()

add_executable(CameraPolicyTests
    Tests/CameraPolicyTests.cpp
    Core/CameraPolicy.cpp
)
target_include_directories(CameraPolicyTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(CameraPolicyTests PRIVATE /utf-8)
endif()

add_executable(CameraZoomTests
    Tests/CameraZoomTests.cpp
    Core/PatternScanner.cpp
    Core/SymbolResolver.cpp
    Core/HookManager.cpp
    Games/Genshin/GenshinCameraBridge.cpp
    Games/Genshin/Features/CameraTweaks/CameraZoom.cpp
)
target_link_libraries(CameraZoomTests PRIVATE minhook)
target_include_directories(CameraZoomTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(CameraZoomTests PRIVATE /utf-8)
endif()

target_include_directories(SSMT-Player-Tweaks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
