#pragma once

#include <windows.h>
#include <stdio.h>
#include <string>
#include <tlhelp32.h>
#include <vector>
#include <shellapi.h>
#include <filesystem>
#include <cstdint>

#include "D3dxIniUtils.hpp"
#include "InjectorUtils.hpp"
#include "LaunchEvents.hpp"
#include "LoaderOptions.hpp"

typedef NTSTATUS(NTAPI *pNtCreateThreadEx)(PHANDLE, ACCESS_MASK, PVOID, HANDLE, PVOID, PVOID, ULONG, ULONG_PTR, SIZE_T, SIZE_T, PVOID);
typedef NTSTATUS(NTAPI *pNtAllocateVirtualMemory)(HANDLE, PVOID *, ULONG_PTR, PSIZE_T, ULONG, ULONG);
typedef NTSTATUS(NTAPI *pNtWriteVirtualMemory)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);

inline pNtCreateThreadEx ResolveNtCreateThreadEx()
{
    return reinterpret_cast<pNtCreateThreadEx>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateThreadEx"));
}

inline pNtAllocateVirtualMemory ResolveNtAllocateVirtualMemory()
{
    return reinterpret_cast<pNtAllocateVirtualMemory>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtAllocateVirtualMemory"));
}

inline pNtWriteVirtualMemory ResolveNtWriteVirtualMemory()
{
    return reinterpret_cast<pNtWriteVirtualMemory>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtWriteVirtualMemory"));
}

class HybridNtInjectorUtils
{
public:
    static bool Run(
        D3dxIniUtils &ini,
        const LoaderOptions &options,
        HANDLE machine_output = GetStdHandle(STD_OUTPUT_HANDLE))
    {
        LaunchEventEmitter events(options.machine_readable, machine_output);
        printf("[Loader] Starting injection.\n");
        if (ini.launch.empty())
        {
            if (options.preload_runtime)
            {
                events.emit_error("preflight", "preload_requires_launch", "Runtime preloading requires launch mode");
                return false;
            }
            printf("[Loader] No launch configured; waiting for target process.\n");
            return FallbackWaitMode(ini, events);
        }

        return LaunchAndInject(
            ini,
            options,
            ini.delay,
            events);
    }

private:
    static bool WaitBeforeResume(
        const LoaderOptions &options,
        const PROCESS_INFORMATION &process,
        LaunchEventEmitter &events)
    {
        if (!options.launch_barrier_id)
            return true;

        const std::wstring prefix = L"Local\\SSMT4.Launch." + *options.launch_barrier_id;
        const std::wstring readyName = prefix + L".Ready";
        const std::wstring releaseName = prefix + L".Release";
        HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, readyName.c_str());
        HANDLE release = OpenEventW(SYNCHRONIZE, FALSE, releaseName.c_str());
        if (!ready || !release)
        {
            events.emit_error("before_resume", "barrier_open_failed", "Could not open launch barrier events");
            if (ready)
                CloseHandle(ready);
            if (release)
                CloseHandle(release);
            return false;
        }

        events.emit("waiting_before_resume");
        const bool signaled = SetEvent(ready) != FALSE;
        const DWORD waitResult = signaled ? WaitForSingleObject(release, INFINITE) : WAIT_FAILED;
        CloseHandle(ready);
        CloseHandle(release);
        if (waitResult != WAIT_OBJECT_0)
        {
            events.emit_error("before_resume", "barrier_wait_failed", "Launch barrier wait failed");
            return false;
        }
        return true;
    }

    static void WaitForExit(const char *msg = "\nPress Enter to close...\n")
    {
        printf("%s", msg);
        getchar();
    }

    static void AutoExitOrWait(const std::wstring &delayStr)
    {
        int delay = ParseDelay(delayStr);
        if (delay == -1)
        {
            WaitForExit();
            return;
        }

        for (int i = delay; i > 0; --i)
        {
            printf("Closing loader in %i...\r", i);
            Sleep(1000);
        }
        printf("\n");
    }

    static bool LaunchAndInject(
        D3dxIniUtils &ini,
        const LoaderOptions &options,
        const std::wstring &delayStr,
        LaunchEventEmitter &events)
    {
        printf("[Loader] Launch-and-inject mode selected.\n");
        HHOOK d3d11Hook = nullptr;
        std::filesystem::path preloadPath;
        if (options.preload_runtime)
        {
            preloadPath = std::filesystem::absolute(ini.module);
            if (!std::filesystem::is_regular_file(preloadPath))
            {
                events.emit_error("preflight", "runtime_missing", "Runtime DLL does not exist");
                return false;
            }
        }
        else
        {
            d3d11Hook = InstallGlobalCbtHook(ini.module.c_str());
        }
        if (!options.preload_runtime && !d3d11Hook)
        {
            events.emit_error("prepare", "hook_install_failed", "Failed to install global CBT hook");
            return false;
        }

        wchar_t runPath[MAX_PATH] = {};
        wchar_t runArgs[MAX_PATH] = {};
        wchar_t workingDir[MAX_PATH] = {};
        wcsncpy_s(runPath, MAX_PATH, ini.launch.c_str(), _TRUNCATE);
        wcsncpy_s(runArgs, MAX_PATH, ini.launch_args.c_str(), _TRUNCATE);

        wchar_t *filePart = nullptr;
        DWORD pathLen = GetFullPathNameW(runPath, MAX_PATH, workingDir, &filePart);
        if (pathLen == 0 || pathLen >= MAX_PATH || !filePart)
        {
            printf("[Loader] Failed to resolve launch working directory: %lu\n", GetLastError());
            events.emit_error("target_launch", "working_directory_failed", "Failed to resolve launch working directory");
            if (d3d11Hook)
                UnhookWindowsHookEx(d3d11Hook);
            return false;
        }
        *filePart = L'\0';

        std::wstring cmdLine = L"\"";
        cmdLine += runPath;
        cmdLine += L"\"";
        if (wcslen(runArgs) > 0)
        {
            cmdLine += L" ";
            cmdLine += runArgs;
        }

        std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
        cmdBuf.push_back(L'\0');

        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};

        printf("[Loader] Launching target suspended: %S\n", runPath);
        if (!CreateProcessW(
                runPath,
                cmdBuf.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_SUSPENDED,
                nullptr,
                workingDir,
                &si,
                &pi))
        {
            printf("[Loader] CreateProcess failed: %lu\n", GetLastError());
            events.emit_error("target_launch", "create_process_failed", "CreateProcess failed");
            if (d3d11Hook)
                UnhookWindowsHookEx(d3d11Hook);
            return false;
        }

        events.emit_pid("target_created", pi.dwProcessId);

        if (options.preload_runtime)
        {
            const auto runtimePath = preloadPath.wstring();
            events.emit_module("inject_begin", runtimePath.c_str());
            if (!NtInjectDll(pi.hProcess, runtimePath.c_str()))
            {
                events.emit_error("inject", "runtime_preload_failed", "Runtime preload failed");
                TerminateProcess(pi.hProcess, ERROR_DLL_INIT_FAILED);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                return false;
            }
            events.emit_module("inject_complete", runtimePath.c_str());
        }

        bool extraOk = true;

        for (size_t index = 0;
             index < ini.inject_dlls.size();
             ++index)
        {
            const auto &dllPath =
                ini.inject_dlls[index];

            printf(
                "[Loader] Injecting extra DLL %zu/%zu: %S\n",
                index + 1,
                ini.inject_dlls.size(),
                dllPath.c_str());

            events.emit_module("inject_begin", dllPath.c_str());

            const bool currentOk =
                NtInjectDll(
                    pi.hProcess,
                    dllPath.c_str());

            if (!currentOk)
            {
                printf(
                    "[Loader] Failed to inject extra DLL: %S\n",
                    dllPath.c_str());
                events.emit_error("inject", "inject_failed", "Failed to inject extra DLL");
            }

            if (currentOk)
                events.emit_module("inject_complete", dllPath.c_str());

            extraOk =
                extraOk && currentOk;
        }

        bool pluginHostOk = true;

        if (options.plugin_host_config)
        {
            events.emit("plugin_host_begin");
            const auto hostPath =
                GetSiblingPath(L"SSMT-PluginHost.dll");

            if (hostPath.empty() ||
                !std::filesystem::exists(hostPath))
            {
                printf(
                    "[Loader] PluginHost DLL not found: %S\n",
                    hostPath.c_str());

                events.emit_error("plugin_host", "plugin_host_missing", "PluginHost DLL not found");

                pluginHostOk = false;
            }
            else
            {
                printf(
                    "[Loader] Injecting PluginHost: %S\n",
                    hostPath.c_str());

                pluginHostOk =
                    NtInjectDll(
                        pi.hProcess,
                        hostPath.c_str());

                if (pluginHostOk)
                {
                    pluginHostOk =
                        StartPluginHost(
                            pi.hProcess,
                            pi.dwProcessId,
                            hostPath,
                        *options.plugin_host_config);
                    if (pluginHostOk)
                        events.emit("plugin_host_ready");
                }
                if (!pluginHostOk)
                    events.emit_error("plugin_host", "plugin_host_start_failed", "PluginHost failed to start");
            }
        }

        // Never resume a target after a required injection failed. Continuing
        // here leaves the game in a partially injected state: the caller sees
        // a launch attempt, while the target can later fail during graphics
        // initialization with no useful loader error. The process is still
        // suspended, so cleanup is deterministic and does not touch an
        // already-running game.
        if (!extraOk || !pluginHostOk)
        {
            events.emit_error("preflight", "required_injection_failed", "Required DLL injection failed");
            TerminateProcess(pi.hProcess, ERROR_DLL_INIT_FAILED);
            if (d3d11Hook)
                UnhookWindowsHookEx(d3d11Hook);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return false;
        }

        if (!WaitBeforeResume(options, pi, events))
        {
            TerminateProcess(pi.hProcess, ERROR_CANCELLED);
            if (d3d11Hook)
                UnhookWindowsHookEx(d3d11Hook);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return false;
        }

        events.emit("before_resume");
        const DWORD previousSuspendCount =
            ResumeThread(pi.hThread);

        if (previousSuspendCount == static_cast<DWORD>(-1))
        {
            printf(
                "[Loader] ResumeThread failed: %lu\n",
                GetLastError());
            events.emit_error("resume", "resume_failed", "ResumeThread failed");
        }
        else
        {
            printf(
                "[Loader] Target resumed. "
                "Previous suspend count: %lu\n",
                previousSuspendCount);
            events.emit("target_resumed");
        }

        const ModuleWaitResult moduleResult = WaitForModuleLoaded(pi.dwProcessId, ini.module.c_str(), 30000);

        bool moduleOk = false;
        switch (moduleResult)
        {
        case ModuleWaitResult::Loaded:
            printf(
                "[Loader] 3DMigoto module loaded.\n");
            moduleOk = true;
            events.emit("runtime_detected");
            break;

        case ModuleWaitResult::VerificationDenied:
            printf(
                "[Loader] 3DMigoto module verification was denied; "
                "injection status is unknown.\n");
            moduleOk = true;
            // 受保护进程仍按原有启动结果处理，但未观测到模块时不能报告已检测到运行时。
            events.emit("runtime_verification_unavailable");
            break;

        case ModuleWaitResult::TimedOut:
            printf(
                "[Loader] Timed out waiting for 3DMigoto module.\n");
            moduleOk = false;
            events.emit_error("runtime", "runtime_timeout", "Timed out waiting for 3DMigoto module");
            break;
        }
        // if (moduleLoaded)
        // {
        //     printf("[Loader] 3DMigoto module loaded.\n");
        // }
        // else
        // {
        //     printf("[Loader] Timed out waiting for 3DMigoto module.\n");
        // }

        if (d3d11Hook)
            UnhookWindowsHookEx(d3d11Hook);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        const bool ok = moduleOk && extraOk && pluginHostOk;
        if (ok)
            events.emit("launch_complete");
        else
            events.emit_error("launch", "launch_failed", "Launch completed with errors");
        AutoExitOrWait(delayStr);
        return ok;
    }

    static bool FallbackWaitMode(D3dxIniUtils &ini, LaunchEventEmitter &events)
    {
        printf("[Loader] Fallback wait mode selected.\n");
        HHOOK d3d11Hook = InstallGlobalCbtHook(ini.module.c_str());
        if (!d3d11Hook)
        {
            events.emit_error("prepare", "hook_install_failed", "Failed to install global CBT hook");
            return false;
        }

        wchar_t moduleFullPath[MAX_PATH] = {};
        HMODULE localModule = LoadLibraryW(ini.module.c_str());
        if (!localModule || !GetModuleFileNameW(localModule, moduleFullPath, MAX_PATH))
        {
            printf("[Loader] Failed to load 3DMigoto module: %lu\n", GetLastError());
            events.emit_error("runtime", "module_load_failed", "Failed to load 3DMigoto module");
            UnhookWindowsHookEx(d3d11Hook);
            return false;
        }

        printf("[Loader] Loaded module: %S\n", moduleFullPath);
        const bool ok = InjectorUtils::WaitForTarget(
            ini.ToByteString(ini.target).c_str(),
            moduleFullPath,
            true,
            ParseDelay(ini.delay),
            false);

        UnhookWindowsHookEx(d3d11Hook);
        if (ok)
        {
            events.emit("runtime_detected");
            events.emit("launch_complete");
        }
        else
        {
            events.emit_error("runtime", "runtime_not_detected", "3DMigoto runtime was not detected");
        }
        return ok;
    }

    static int ParseDelay(const std::wstring &delayStr)
    {
        int delay = 5;
        try
        {
            delay = std::stoi(delayStr);
        }
        catch (...)
        {
            delay = 5;
        }
        return (delay <= 0 && delay != -1) ? 5 : delay;
    }

    static ModuleWaitResult WaitForModuleLoaded(
        DWORD pid,
        const wchar_t *modulePath,
        DWORD timeoutMs)
    {
        const wchar_t *base =
            wcsrchr(modulePath, L'\\');

        base = base
                   ? base + 1
                   : modulePath;

        DWORD waited = 0;
        bool verificationDenied = false;

        while (waited < timeoutMs)
        {
            HANDLE snap =
                CreateToolhelp32Snapshot(
                    TH32CS_SNAPMODULE |
                        TH32CS_SNAPMODULE32,
                    pid);

            if (snap == INVALID_HANDLE_VALUE)
            {
                const DWORD error =
                    GetLastError();

                if (error == ERROR_ACCESS_DENIED)
                {
                    if (!verificationDenied)
                    {
                        printf(
                            "[Loader] Module verification denied; "
                            "keeping CBT hook active briefly.\n");
                    }

                    verificationDenied = true;
                }
                else
                {
                    printf(
                        "[Loader] Module snapshot failed: %lu\n",
                        error);
                }

                Sleep(500);
                waited += 500;

                /*
                 * 已经明确知道枚举永远会被拒绝，
                 * 没必要傻等整整 30 秒。
                 *
                 * 这里保留约 5 秒，
                 * 给恢复后的目标进程和 CBT hook 留时间。
                 */
                if (verificationDenied && waited >= 5000)
                {
                    return ModuleWaitResult::VerificationDenied;
                }

                continue;
            }

            MODULEENTRY32W me{};
            me.dwSize = sizeof(me);

            if (Module32FirstW(snap, &me))
            {
                do
                {
                    if (_wcsicmp(me.szModule, base) == 0)
                    {
                        CloseHandle(snap);
                        return ModuleWaitResult::Loaded;
                    }
                } while (Module32NextW(snap, &me));
            }

            CloseHandle(snap);

            Sleep(500);
            waited += 500;
        }

        return verificationDenied
                   ? ModuleWaitResult::VerificationDenied
                   : ModuleWaitResult::TimedOut;
    }

    static HHOOK InstallGlobalCbtHook(const wchar_t *d3d11Path)
    {
        HMODULE localModule = LoadLibraryW(d3d11Path);
        if (!localModule)
        {
            printf("[Loader] Failed to load %S: %lu\n", d3d11Path, GetLastError());
            return nullptr;
        }

        FARPROC cbt = GetProcAddress(localModule, "CBTProc");
        if (!cbt)
        {
            printf("[Loader] %S does not export CBTProc.\n", d3d11Path);
            return nullptr;
        }

        HHOOK hook = SetWindowsHookExW(WH_CBT, reinterpret_cast<HOOKPROC>(cbt), localModule, 0);
        if (!hook)
        {
            printf("[Loader] SetWindowsHookEx failed: %lu\n", GetLastError());
            return nullptr;
        }

        printf("[Loader] Global CBT hook installed from %S.\n", d3d11Path);
        return hook;
    }

    static std::filesystem::path GetSiblingPath(
        const wchar_t *fileName)
    {
        wchar_t loaderPath[MAX_PATH]{};

        const DWORD length =
            GetModuleFileNameW(
                nullptr,
                loaderPath,
                MAX_PATH);

        if (length == 0 || length >= MAX_PATH)
        {
            printf(
                "[Loader] GetModuleFIleNameW failed: %lu\n",
                GetLastError());

            return {};
        }

        return std::filesystem::path{
                   loaderPath}
                   .parent_path() /
               fileName;
    }

    static std::uintptr_t GetExportRva(
        const std::filesystem::path &dllPath,
        const char *exportName)
    {
        HMODULE module = LoadLibraryW(
            dllPath.c_str());

        if (!module)
        {
            printf(
                "[Loader] Failed to load local DLL for export lookup: %S, error=%lu\n",
                dllPath.c_str(),
                GetLastError());

            return 0;
        }

        FARPROC proc =
            GetProcAddress(
                module,
                exportName);

        if (!proc)
        {
            printf(
                "[Loader] Export not found: %s, error=%lu\n",
                exportName,
                GetLastError());

            FreeLibrary(module);
            return 0;
        }

        const auto moduleAddress =
            reinterpret_cast<std::uintptr_t>(module);

        const auto procAddress =
            reinterpret_cast<std::uintptr_t>(proc);

        const std::uintptr_t rva =
            procAddress - moduleAddress;

        FreeLibrary(module);

        return rva;
    }

    static std::uintptr_t FindRemoteModuleBase(
        DWORD processId,
        const std::filesystem::path &modulePath)
    {
        const std::wstring moduleName =
            modulePath.filename().wstring();

        constexpr int maxAttempts = 20;

        for (int attempt = 0;
             attempt < maxAttempts;
             ++attempt)
        {
            HANDLE snapshot =
                CreateToolhelp32Snapshot(
                    TH32CS_SNAPMODULE |
                        TH32CS_SNAPMODULE32,
                    processId);

            if (snapshot == INVALID_HANDLE_VALUE)
            {
                const DWORD error =
                    GetLastError();

                if (error == ERROR_BAD_LENGTH)
                {
                    Sleep(50);
                    continue;
                }

                printf(
                    "[Loader] Failed to enumerate remote modules: %lu\n",
                    error);

                return 0;
            }

            MODULEENTRY32W entry{};
            entry.dwSize =
                sizeof(entry);

            if (Module32FirstW(
                    snapshot,
                    &entry))
            {
                do
                {
                    if (_wcsicmp(
                            entry.szModule,
                            moduleName.c_str()) == 0)
                    {
                        const auto base =
                            reinterpret_cast<std::uintptr_t>(
                                entry.modBaseAddr);

                        CloseHandle(snapshot);

                        return base;
                    }
                } while (Module32NextW(
                    snapshot,
                    &entry));
            }

            CloseHandle(snapshot);

            Sleep(50);
        }

        printf(
            "[Loader] Remote module not found: %S\n",
            moduleName.c_str());

        return 0;
    }

    static bool StartPluginHost(
        HANDLE process,
        DWORD processId,
        const std::filesystem::path &hostPath,
        const std::filesystem::path &configPath)
    {
        const std::uintptr_t remoteBase =
            FindRemoteModuleBase(
                processId,
                hostPath);

        if (remoteBase == 0)
            return false;

        const std::uintptr_t entryRva =
            GetExportRva(
                hostPath,
                "SSMTPluginHost_Main");

        if (entryRva == 0)
            return false;

        const std::uintptr_t remoteEntry =
            remoteBase + entryRva;

        printf(
            "[Loader] PluginHost remote base: 0x%llX\n",
            static_cast<unsigned long long>(remoteBase));

        printf(
            "[Loader] PluginHost entry RVA: 0x%llX\n",
            static_cast<unsigned long long>(entryRva));

        printf(
            "[Loader] PluginHost remote entry: 0x%llX\n",
            static_cast<unsigned long long>(remoteEntry));

        const auto absoluteConfigPath =
            std::filesystem::absolute(configPath);

        printf(
            "[Loader] PluginHost config: %S\n",
            absoluteConfigPath.c_str());

        PVOID remoteConfigPath =
            WriteRemoteWideString(
                process,
                absoluteConfigPath.wstring());

        if (!remoteConfigPath)
            return false;

        printf(
            "[Loader] Remote config path: %p\n",
            remoteConfigPath);

        DWORD hostExitCode = 0;

        const bool started =
            RunRemoteEntryPoint(
                process,
                reinterpret_cast<PVOID>(remoteEntry),
                remoteConfigPath,
                hostExitCode);

        if (!started)
            return false;

        printf(
            "[Loader] PluginHost exit code: 0x%08lX\n",
            hostExitCode);

        if (hostExitCode != 0)
        {
            printf(
                "[Loader] PluginHost startup failed.\n");

            return false;
        }

        printf(
            "[Loader] PluginHost started successfully.\n");

        return true;
    }

    static bool NtInjectDll(HANDLE process, const wchar_t *dllPath)
    {
        printf("[Loader] NtInjectDll begin: %S\n", dllPath);

        auto ntAlloc = ResolveNtAllocateVirtualMemory();
        auto ntWrite = ResolveNtWriteVirtualMemory();
        auto ntThread = ResolveNtCreateThreadEx();

        if (!ntAlloc || !ntWrite || !ntThread)
        {
            printf("[Loader] Failed to resolve NT injection functions.\n");
            return false;
        }
#ifdef _DEBUG
        printf("[Loader] NT functions resolved.\n");
#endif
        const SIZE_T pathBytes =
            (wcslen(dllPath) + 1) * sizeof(wchar_t);
        SIZE_T allocationSize = pathBytes;

        PVOID remote = nullptr;
#ifdef _DEBUG
        printf(
            "[Loader] Allocating %zu bytes in target process...\n",
            pathBytes);
#endif
        NTSTATUS status = ntAlloc(
            process,
            &remote,
            0,
            &allocationSize,
            MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE);

        if (status != 0)
        {
            printf(
                "[Loader] NtAllocateVirtualMemory failed: 0x%X\n",
                status);
            return false;
        }
#ifdef _DEBUG
        printf(
            "[Loader] Remote memory allocated at %p.\n",
            remote);
#endif
        SIZE_T written = 0;

        printf("[Loader] Writing DLL path to target...\n");

        status = ntWrite(
            process,
            remote,
            const_cast<wchar_t *>(dllPath),
            pathBytes,
            &written);

        if (status != 0 || written != pathBytes)
        {
            printf(
                "[Loader] NtWriteVirtualMemory failed: "
                "status=0x%X, written=%zu/%zu\n",
                status,
                written,
                pathBytes);
            return false;
        }
#ifdef _DEBUG
        printf("[Loader] DLL path written successfully.\n");
#endif
        PVOID loadLibrary =
            reinterpret_cast<PVOID>(
                GetProcAddress(
                    GetModuleHandleW(L"kernel32.dll"),
                    "LoadLibraryW"));

        if (!loadLibrary)
        {
            printf(
                "[Loader] Failed to resolve LoadLibraryW.\n");
            return false;
        }
#ifdef _DEBUG
        printf(
            "[Loader] Local LoadLibraryW address: %p\n",
            loadLibrary);
#endif
        HANDLE thread = nullptr;
#ifdef _DEBUG
        printf(
            "[Loader] Creating remote LoadLibraryW thread...\n");
#endif
        status = ntThread(
            &thread,
            THREAD_ALL_ACCESS,
            nullptr,
            process,
            loadLibrary,
            remote,
            0,
            0,
            0,
            0,
            nullptr);

        if (status != 0 || !thread)
        {
            printf(
                "[Loader] NtCreateThreadEx failed: 0x%X\n",
                status);
            return false;
        }
#ifdef _DEBUG
        printf(
            "[Loader] Remote thread created: %p\n",
            thread);

        printf(
            "[Loader] Waiting for remote LoadLibraryW...\n");
#endif
        const DWORD waitResult =
            WaitForSingleObject(
                thread,
                INFINITE);
#ifdef _DEBUG
        printf(
            "[Loader] WaitForSingleObject returned: 0x%08lX\n",
            waitResult);
#endif
        DWORD exitCode = 0;

        if (!GetExitCodeThread(thread, &exitCode))
        {
            printf(
                "[Loader] GetExitCodeThread failed: %lu\n",
                GetLastError());

            CloseHandle(thread);
            return false;
        }
#ifdef _DEBUG
        printf(
            "[Loader] Remote thread exit code: 0x%08lX\n",
            exitCode);

        CloseHandle(thread);
#endif
        if (exitCode == 0)
        {
            printf(
                "[Loader] Remote LoadLibraryW failed for %S.\n",
                dllPath);
            return false;
        }

        printf(
            "[Loader] NtInjectDll success: %S\n",
            dllPath);

        return true;
    }

    static PVOID WriteRemoteWideString(
        HANDLE process,
        const std::wstring &value)
    {
        auto ntAlloc =
            ResolveNtAllocateVirtualMemory();

        auto ntWrite =
            ResolveNtWriteVirtualMemory();

        if (!ntAlloc || !ntWrite)
        {
            printf(
                "[Loader] Failed to resolve NT memory functions.\n");

            return nullptr;
        }

        const SIZE_T stringBytes = (value.size() + 1) * sizeof(wchar_t);
        SIZE_T allocationSize = stringBytes;

        PVOID remote = nullptr;

        NTSTATUS status =
            ntAlloc(
                process,
                &remote,
                0,
                &allocationSize,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE);

        if (status != 0)
        {
            printf(
                "[Loader] Failed to allocate remote string: 0x %X\n",
                status);

            return nullptr;
        }

        SIZE_T written = 0;

        status = ntWrite(
            process,
            remote,
            const_cast<wchar_t *>(
                value.c_str()),
            stringBytes,
            &written);

        if (status != 0 ||
            written != stringBytes)
        {
            printf(
                "[Loader] Failed to write remote string: "
                "status=0x%X, written=%zu/%zu\n",
                status,
                written,
                stringBytes);

            return nullptr;
        }

        return remote;
    }

    static bool RunRemoteEntryPoint(
        HANDLE process,
        PVOID entryPoint,
        PVOID parameter,
        DWORD &exitCode)
    {
        auto ntThread = ResolveNtCreateThreadEx();

        if (!ntThread)
        {
            printf(
                "[Loader] Failed to resolve NtCreateThreadEx.\n");

            return false;
        }

        HANDLE thread = nullptr;

        const NTSTATUS status =
            ntThread(
                &thread,
                THREAD_ALL_ACCESS,
                nullptr,
                process,
                entryPoint,
                parameter,
                0, 0, 0, 0,
                nullptr);

        if (status != 0 ||
            !thread)
        {
            printf(
                "[Loader] Failed to create PluginHost thread: 0x%X\n",
                status);

            return false;
        }

        const DWORD waitResult = WaitForSingleObject(
            thread, INFINITE);

        if (waitResult != WAIT_OBJECT_0)
        {
            printf(
                "[Loader] Waiting for PluginHost failed: 0x%08lX\n",
                waitResult);

            CloseHandle(thread);
            return false;
        }

        if (!GetExitCodeThread(thread, &exitCode))
        {
            printf(
                "[Loader] GetExitCodeThread failed: %lu\n",
                GetLastError());

            CloseHandle(thread);
            return false;
        }

        CloseHandle(thread);
        return true;
    }
};
