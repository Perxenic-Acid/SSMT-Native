cmake_minimum_required(VERSION 3.24)
project(SSMTPoser LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "SSMT-Poser requires Windows x64 and MSVC (SEH)")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# 复用工作区已有 MinHook；独立编译进探针，不加载 Player Tweaks 插件。
if(NOT TARGET minhook)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../SSMT-Player-Tweaks/ThirdParty/MinHook"
        "${CMAKE_CURRENT_BINARY_DIR}/PoserMinHook")
endif()

add_library(SSMT-Poser SHARED Plugin.cpp RuntimeProbe.cpp GenshinNativeRuntime.cpp LiveUnityProbe.cpp RigProbe.cpp BoneWriteProbe.cpp VmdMotion.cpp MotionPlayback.cpp)
target_link_libraries(SSMT-Poser PRIVATE bcrypt user32 minhook)
target_compile_options(SSMT-Poser PRIVATE /utf-8 /W4 /EHsc)
target_compile_definitions(SSMT-Poser PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
set(SSMT_POSER_ARTIFACT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dist/$<CONFIG>"
    CACHE STRING "探针 DLL 输出目录，可在实机占用旧产物时指定新目录")
add_custom_command(TARGET SSMT-Poser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SSMT_POSER_ARTIFACT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SSMT-Poser>"
        "${SSMT_POSER_ARTIFACT_DIR}/SSMT-Poser.dll")

add_executable(PoserProbeTests Tests/ProbeTests.cpp RuntimeProbe.cpp GenshinNativeRuntime.cpp LiveUnityProbe.cpp RigProbe.cpp BoneWriteProbe.cpp VmdMotion.cpp MotionPlayback.cpp)
target_link_libraries(PoserProbeTests PRIVATE bcrypt user32 minhook)
target_compile_options(PoserProbeTests PRIVATE /utf-8 /W4 /EHsc)
target_compile_definitions(PoserProbeTests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(PoserProbeTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
enable_testing()
add_test(NAME PoserProbeTests COMMAND PoserProbeTests "$<TARGET_FILE:SSMT-Poser>")
