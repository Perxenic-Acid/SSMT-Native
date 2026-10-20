#pragma once

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace DirectInjector
{
    class Handle
    {
    public:
        explicit Handle(HANDLE value = nullptr) : value_(value) {}
        ~Handle()
        {
            if (*this)
                CloseHandle(value_);
        }
        Handle(const Handle &) = delete;
        Handle &operator=(const Handle &) = delete;
        HANDLE get() const { return value_; }
        explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }

    private:
        HANDLE value_;
    };

    inline bool SameFile(const std::filesystem::path &left, const std::filesystem::path &right)
    {
        std::error_code error;
        return std::filesystem::equivalent(left, right, error) && !error;
    }

    inline bool MatchesTarget(HANDLE process, const std::filesystem::path &target)
    {
        std::vector<wchar_t> path(32768);
        DWORD size = static_cast<DWORD>(path.size());
        return QueryFullProcessImageNameW(process, 0, path.data(), &size) &&
               SameFile(std::wstring(path.data(), size), target);
    }

    // 返回注入所需权限的句柄，并用同一句柄复核路径，避免误选同名程序或 PID 复用。
    inline HANDLE WaitForTarget(const std::filesystem::path &target, DWORD timeoutMs, DWORD &pid)
    {
        const auto name = target.filename().wstring();
        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        std::set<DWORD> reported;
        do
        {
            Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
            if (!snapshot)
            {
                printf("[Loader] Process snapshot failed: win32=%lu\n", GetLastError());
                return nullptr;
            }
            PROCESSENTRY32W entry{sizeof(entry)};
            if (Process32FirstW(snapshot.get(), &entry))
            {
                do
                {
                    if (_wcsicmp(entry.szExeFile, name.c_str()) != 0)
                        continue;
                    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE,
                        FALSE, entry.th32ProcessID);
                    if (!process)
                    {
                        const DWORD error = GetLastError();
                        if (reported.insert(entry.th32ProcessID).second)
                            printf("[Loader] OpenProcess PID=%lu failed: win32=%lu; retrying until timeout.\n",
                                entry.th32ProcessID, error);
                        continue;
                    }
                    if (MatchesTarget(process, target) && WaitForSingleObject(process, 0) == WAIT_TIMEOUT)
                    {
                        pid = entry.th32ProcessID;
                        printf("[Loader] Target detected: PID=%lu, parent PID=%lu, path=%S\n",
                            pid, entry.th32ParentProcessID, target.c_str());
                        return process;
                    }
                    CloseHandle(process);
                } while (Process32NextW(snapshot.get(), &entry));
            }
            Sleep(100);
        } while (GetTickCount64() < deadline);
        printf("[Loader] Target wait timed out after %lu ms. Start the game through its official launcher, "
            "enable DirectX 11, and verify the configured executable path and elevation.\n", timeoutMs);
        return nullptr;
    }

    inline std::uintptr_t FindModule(DWORD pid, const std::filesystem::path &path, bool exactPath)
    {
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
        if (!snapshot)
            return 0;
        MODULEENTRY32W entry{sizeof(entry)};
        if (!Module32FirstW(snapshot.get(), &entry))
            return 0;
        const auto name = path.filename().wstring();
        do
        {
            if (_wcsicmp(entry.szModule, name.c_str()) == 0 &&
                (!exactPath || SameFile(entry.szExePath, path)))
                return reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
        } while (Module32NextW(snapshot.get(), &entry));
        return 0;
    }

    inline bool InjectDll(HANDLE process, DWORD pid, const std::filesystem::path &path)
    {
        if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT)
        {
            printf("[Loader] Target exited before injection: PID=%lu\n", pid);
            return false;
        }
        BOOL localWow64 = FALSE;
        BOOL remoteWow64 = FALSE;
        if (!IsWow64Process(GetCurrentProcess(), &localWow64) || !IsWow64Process(process, &remoteWow64) ||
            localWow64 != remoteWow64)
        {
            printf("[Loader] Injector/target architecture mismatch or query failed: PID=%lu, win32=%lu\n",
                pid, GetLastError());
            return false;
        }
        // 系统 d3d11.dll 与我们的运行时可同名；只有完整文件身份相符才算已加载。
        if (FindModule(pid, path, true))
            return true;

        const auto loadLibrary = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        HMODULE owner = nullptr;
        if (!loadLibrary || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(loadLibrary), &owner))
        {
            printf("[Loader] Resolve LoadLibraryW failed: win32=%lu\n", GetLastError());
            return false;
        }
        wchar_t ownerPath[MAX_PATH]{};
        if (!GetModuleFileNameW(owner, ownerPath, MAX_PATH))
            return false;
        // LoadLibraryW 可能转发到 KernelBase。按实际所属模块的 RVA 解析远端地址。
        std::uintptr_t remoteBase = 0;
        const ULONGLONG deadline = GetTickCount64() + 15000;
        do
        {
            remoteBase = FindModule(pid, ownerPath, false);
            if (remoteBase || WaitForSingleObject(process, 0) != WAIT_TIMEOUT)
                break;
            Sleep(10);
        } while (GetTickCount64() < deadline);
        if (!remoteBase)
        {
            printf("[Loader] Remote LoadLibraryW module unavailable: PID=%lu, module=%S, win32=%lu\n",
                pid, ownerPath, GetLastError());
            return false;
        }
        const auto remoteEntry = remoteBase + reinterpret_cast<std::uintptr_t>(loadLibrary) -
            reinterpret_cast<std::uintptr_t>(owner);
        const auto dllPath = path.wstring();
        const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
        void *remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remote)
        {
            printf("[Loader] VirtualAllocEx failed: PID=%lu, win32=%lu\n", pid, GetLastError());
            return false;
        }
        SIZE_T written = 0;
        if (!WriteProcessMemory(process, remote, dllPath.c_str(), bytes, &written) || written != bytes)
        {
            printf("[Loader] WriteProcessMemory failed: PID=%lu, win32=%lu\n", pid, GetLastError());
            VirtualFreeEx(process, remote, 0, MEM_RELEASE);
            return false;
        }
        Handle thread(CreateRemoteThread(process, nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteEntry), remote, 0, nullptr));
        if (!thread)
        {
            printf("[Loader] CreateRemoteThread failed: PID=%lu, win32=%lu\n", pid, GetLastError());
            VirtualFreeEx(process, remote, 0, MEM_RELEASE);
            return false;
        }
        const DWORD result = WaitForSingleObject(thread.get(), 15000);
        if (result != WAIT_OBJECT_0)
        {
            printf("[Loader] LoadLibraryW wait failed: PID=%lu, result=%lu, win32=%lu\n",
                pid, result, GetLastError());
            // 超时不结束游戏或远程线程；线程仍可能读取参数，不能释放该内存。
            return false;
        }
        DWORD exitCode = 0;
        const bool exitCodeRead = GetExitCodeThread(thread.get(), &exitCode) != FALSE;
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        // x64 HMODULE 不能由 DWORD 退出码完整表示，以实际加载的 DLL 路径为准。
        bool loaded = false;
        const ULONGLONG verifyDeadline = GetTickCount64() + 3000;
        do
        {
            if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT)
                break;
            loaded = FindModule(pid, path, true) != 0;
            if (loaded)
                break;
            Sleep(50);
        } while (GetTickCount64() < verifyDeadline);
        printf("[Loader] Direct injection: PID=%lu, module=%S, thread_exit=0x%08lX, "
            "exit_code_read=%d, exact_module_detected=%d\n", pid, path.c_str(), exitCode, exitCodeRead, loaded);
        return loaded;
    }

    // 直接启动时目标 PID 是已知的，必须注入这个进程，不能按同名路径重新扫描，
    // 否则会命中上一次启动残留的 Endfield.exe。
    inline HANDLE OpenTargetByPid(DWORD pid, const std::filesystem::path &target)
    {
        HANDLE process = OpenProcess(
            PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ |
                SYNCHRONIZE,
            FALSE,
            pid);
        if (!process)
        {
            printf("[Loader] OpenProcess PID=%lu failed: win32=%lu\n", pid, GetLastError());
            return nullptr;
        }
        if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT || !MatchesTarget(process, target))
        {
            printf("[Loader] Created target PID=%lu is no longer the expected process\n", pid);
            CloseHandle(process);
            return nullptr;
        }
        return process;
    }
}
