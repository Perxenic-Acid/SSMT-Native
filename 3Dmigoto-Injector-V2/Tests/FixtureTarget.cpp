#include <windows.h>
#include <filesystem>
#include <fstream>

int wmain(int argc, wchar_t *argv[])
{
    if (argc < 3)
        return 2;
    if (GetEnvironmentVariableW(L"SSMT_FIXTURE_ABSENT", nullptr, 0) != 0)
        return 4;
    wchar_t systemDirectory[MAX_PATH]{};
    GetSystemDirectoryW(systemDirectory, MAX_PATH);
    // 预先加载同名系统 DLL，注入器必须识别测试目录中的另一份 DLL。
    LoadLibraryW((std::filesystem::path(systemDirectory) / L"version.dll").c_str());
    const ULONGLONG deadline = GetTickCount64() + 30000;
    while (GetTickCount64() < deadline)
    {
        bool ready = true;
        for (int index = 2; index < argc; ++index)
            ready = ready && GetModuleHandleW(argv[index]) != nullptr;
        if (ready)
        {
            wchar_t environment[2048]{};
            GetEnvironmentVariableW(L"SSMT_FIXTURE_ENV", environment, 2048);
            std::wofstream(std::filesystem::path(argv[1])) << L"injected" << environment;
            Sleep(5000);
            return 0;
        }
        Sleep(10);
    }
    return 3;
}
