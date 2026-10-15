cmake_minimum_required(VERSION 3.20)

project(3Dmigoto-Injector-V2 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


add_executable(3Dmigoto-Injector-V2
    Main.cpp
    3Dmigoto-Injector-V2.rc
    D3dxIniUtils.hpp
    DirectInjector.hpp
    HybridNtInjectorUtils.hpp
    LaunchEvents.hpp
    InjectorUtils.hpp
    LoaderOptions.hpp
    resource.h
)

set_target_properties(3Dmigoto-Injector-V2 PROPERTIES
    OUTPUT_NAME "Run"
)

target_compile_definitions(3Dmigoto-Injector-V2 PRIVATE
    UNICODE
    _UNICODE
    _CONSOLE
)

if(MSVC)
    option(SSMT_INJECTOR_REQUIRE_ADMIN "Require elevation for the injector" ON)
    target_compile_options(3Dmigoto-Injector-V2 PRIVATE
        /W3
        /permissive-
        /sdl
        /utf-8
    )

    if(SSMT_INJECTOR_REQUIRE_ADMIN)
        set(SSMT_INJECTOR_UAC_LEVEL requireAdministrator)
    else()
        set(SSMT_INJECTOR_UAC_LEVEL asInvoker)
    endif()
    target_link_options(3Dmigoto-Injector-V2 PRIVATE
        /MANIFEST:EMBED
        "/MANIFESTUAC:level='${SSMT_INJECTOR_UAC_LEVEL}' uiAccess='false'"
    )
endif()

option(SSMT_INJECTOR_BUILD_TESTS "Build local direct-injection fixtures" OFF)
if(SSMT_INJECTOR_BUILD_TESTS)
    if(SSMT_INJECTOR_REQUIRE_ADMIN)
        message(FATAL_ERROR "Use SSMT_INJECTOR_REQUIRE_ADMIN=OFF in a separate test build directory")
    endif()
    enable_testing()
    add_library(InjectorFixture SHARED Tests/FixtureDll.cpp)
    add_executable(InjectorFixtureTarget Tests/FixtureTarget.cpp)
    target_compile_options(InjectorFixture PRIVATE /utf-8)
    target_compile_options(InjectorFixtureTarget PRIVATE /utf-8)
    add_test(NAME InjectorDirectIntegration
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/Tests/DirectInjectionTests.ps1"
            -LoaderPath "$<TARGET_FILE:3Dmigoto-Injector-V2>"
            -TargetPath "$<TARGET_FILE:InjectorFixtureTarget>"
            -FixtureDllPath "$<TARGET_FILE:InjectorFixture>")
endif()
