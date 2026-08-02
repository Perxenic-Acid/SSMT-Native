add_library(MessageBoxTestDll SHARED
    dllmain.cpp
    pch.cpp
    framework.h
    pch.h
)

target_compile_definitions(MessageBoxTestDll PRIVATE
    UNICODE
    _UNICODE
    MESSAGEBOXTESTDLL_EXPORTS
    _WINDOWS
    _USRDLL
)

target_precompile_headers(MessageBoxTestDll PRIVATE pch.h)

if(MSVC)
    target_compile_options(MessageBoxTestDll PRIVATE
        /W3
        /permissive-
        /sdl
    )
endif()
