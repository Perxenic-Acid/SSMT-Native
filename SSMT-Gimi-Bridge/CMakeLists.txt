find_package(Git REQUIRED)

# 使用与现有 FSR2 转译链相同的 1.2.3 源码，避免引入新版 FFX12 接管路线。
# 固定 commit 与归档哈希，补丁仅存在于构建目录，便于审阅和同步上游。
set(bridge_commit cbdf5c11bfea8d4cb423866dabb96a755fb8ab0b)
set(archive "${CMAKE_BINARY_DIR}/bridge-${bridge_commit}.zip")
set(archive_hash 996cd2ef72df58748d029d2dec525c486e11edc78b7761ae7a4ec7612aee6f8c)
if(EXISTS "${archive}")
    file(SHA256 "${archive}" actual_hash)
endif()
if(NOT actual_hash STREQUAL archive_hash)
    file(DOWNLOAD "https://codeload.github.com/AizawaHikaru233/genshin_fsr_brigde/zip/${bridge_commit}"
        "${archive}" EXPECTED_HASH "SHA256=${archive_hash}" TLS_VERIFY ON TIMEOUT 120)
endif()
set(source_root "${CMAKE_BINARY_DIR}/bridge-source/genshin_fsr_brigde-${bridge_commit}")
set(patch "${CMAKE_CURRENT_SOURCE_DIR}/per-instance-methods.patch")
file(SHA256 "${patch}" patch_hash)
if(EXISTS "${source_root}/ssmt-patch.sha256")
    file(READ "${source_root}/ssmt-patch.sha256" applied_hash)
endif()
if(NOT applied_hash STREQUAL patch_hash)
    # 直接解包，避免 Windows 对繁忙目录 RENAME 失败；不清理任何已有源码目录。
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${CMAKE_BINARY_DIR}/bridge-source")
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --unsafe-paths
        "--directory=${source_root}" "${patch}" RESULT_VARIABLE patch_result)
    if(NOT patch_result EQUAL 0)
        message(FATAL_ERROR "Bridge 适配补丁应用失败：${patch_result}")
    endif()
    file(WRITE "${source_root}/ssmt-patch.sha256" "${patch_hash}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")
set(bridge_source "${source_root}/Dx11FsrBridge")
set(DX11FSRBRIDGE_ENABLE_FSR2_TRANSLATION_EXPERIMENTAL ON CACHE BOOL "" FORCE)
set(DX11FSRBRIDGE_RELEASE_RUNTIME ON CACHE BOOL "" FORCE)
add_subdirectory("${bridge_source}" upstream)
target_include_directories(Dx11FsrBridge PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(Dx11FsrBridge PROPERTIES OUTPUT_NAME SSMT-Gimi-Bridge
    MSVC_RUNTIME_LIBRARY MultiThreaded)

add_custom_target(DeployGimiBridge ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/dist/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Dx11FsrBridge>"
        "${CMAKE_SOURCE_DIR}/dist/$<CONFIG>/SSMT-Gimi-Bridge.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${bridge_source}/LICENSE"
        "${CMAKE_SOURCE_DIR}/dist/$<CONFIG>/SSMT-Gimi-Bridge-LICENSE.txt"
    DEPENDS Dx11FsrBridge)

option(SSMT_GIMI_BRIDGE_BUILD_TESTS "构建 Bridge 混合设备转发回归" OFF)
if(SSMT_GIMI_BRIDGE_BUILD_TESTS)
    enable_testing()
    set(SSMT_GIMI_BRIDGE_TEST_SOURCE "${bridge_source}/Dx11FsrBridge.cpp" CACHE FILEPATH "可选未修复源码，用于验证回归能拒绝旧实现")
    add_executable(gimi-bridge-interop-test tests/main.cpp
        "${bridge_source}/RenderScaleMenu.cpp" "${bridge_source}/Fsr2TranslationLayer.cpp")
    target_compile_features(gimi-bridge-interop-test PRIVATE cxx_std_20)
    set_target_properties(gimi-bridge-interop-test PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreaded)
    target_compile_options(gimi-bridge-interop-test PRIVATE /utf-8 /EHsc)
    target_compile_definitions(gimi-bridge-interop-test PRIVATE
        WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE DX11FSRBRIDGE_RELEASE_RUNTIME=1
        DX11FSRBRIDGE_ENABLE_FSR2_TRANSLATION_EXPERIMENTAL=1
        "SSMT_BRIDGE_SOURCE=\"${SSMT_GIMI_BRIDGE_TEST_SOURCE}\"")
    target_include_directories(gimi-bridge-interop-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${bridge_source}" "${bridge_source}/third_party" "${bridge_source}/third_party/detours")
    target_link_libraries(gimi-bridge-interop-test PRIVATE d3d11 d3dcompiler dxgi
        "${bridge_source}/third_party/detours/detours.lib")
    add_test(NAME gimi-bridge-mixed-objects COMMAND gimi-bridge-interop-test)
    set_tests_properties(gimi-bridge-mixed-objects PROPERTIES TIMEOUT 30)
    set(SSMT_GIMI_BRIDGE_TEST_RUNTIME "" CACHE FILEPATH "可选 x64 Runtime DLL，用于混合真实设备回归")
    if(SSMT_GIMI_BRIDGE_TEST_RUNTIME)
        set(stage "${CMAKE_CURRENT_BINARY_DIR}/runtime-case")
        file(MAKE_DIRECTORY "${stage}")
        file(WRITE "${stage}/d3dx.ini" "[Loader]\ntarget=gimi-bridge-interop-test.exe\n[System]\nload_library_redirect=2\n[Device]\nallow_platform_update=1\n[Hunting]\nhunting=2\n[Logging]\ncalls=1\nunbuffered=1\n")
        add_custom_command(TARGET gimi-bridge-interop-test POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SSMT_GIMI_BRIDGE_TEST_RUNTIME}" "${stage}/d3d11.dll")
        add_test(NAME gimi-bridge-runtime-native-devices COMMAND ${CMAKE_COMMAND}
            "-DFIXTURE=$<TARGET_FILE:gimi-bridge-interop-test>" "-DRUNTIME=${stage}/d3d11.dll"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/RunCase.cmake")
        set_tests_properties(gimi-bridge-runtime-native-devices PROPERTIES TIMEOUT 30)
    endif()
endif()
