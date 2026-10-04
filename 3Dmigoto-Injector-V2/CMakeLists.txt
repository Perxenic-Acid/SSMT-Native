cmake_minimum_required(VERSION 3.20)

project(3Dmigoto-Injector-V2 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


add_executable(3Dmigoto-Injector-V2
    Main.cpp
    3Dmigoto-Injector-V2.rc
    D3dxIniUtils.hpp
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
