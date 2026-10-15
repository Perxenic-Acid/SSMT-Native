#include <windows.h>

// 没有 CBTProc 导出，确保测试实际走直接注入；同时可模拟 PluginHost 的同步入口。
extern "C" __declspec(dllexport) DWORD WINAPI SSMTPluginHost_Main(void *config)
{
    return config && GetFileAttributesW(static_cast<const wchar_t *>(config)) != INVALID_FILE_ATTRIBUTES
        ? ERROR_SUCCESS : ERROR_FILE_NOT_FOUND;
}
