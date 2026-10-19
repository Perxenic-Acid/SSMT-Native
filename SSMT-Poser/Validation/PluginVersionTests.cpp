#include "RuntimeProbe.h"
#include <cstring>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    // 只在独立测试进程查询 ABI，绝不调用 initialize 或扫描游戏。
    const HMODULE library = LoadLibraryW(argv[1]);
    if (!library) return 3;
    using Query = poser::Status (*)(uint32_t, poser::PluginInfo*, poser::PluginApi*) noexcept;
    const auto query = reinterpret_cast<Query>(GetProcAddress(library, "SSMTPlugin_Query"));
    poser::PluginInfo info{sizeof(poser::PluginInfo)};
    poser::PluginApi api{sizeof(poser::PluginApi)};
    const bool matches = query && query(poser::AbiVersion, &info, &api) == poser::Ok &&
        info.version && std::wstring(info.version, info.version + std::strlen(info.version)) == argv[2];
    if (matches) std::cout << "Poser package / DLL version: " << info.version << '\n';
    else std::wcerr << L"Poser DLL version does not match package: expected " << argv[2] << L'\n';
    FreeLibrary(library);
    return matches ? 0 : 1;
}
