#pragma once

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

// EFMI 直接启动需要把 Endfield.exe 的父进程伪装成官方启动器的 Games.exe。
// 该逻辑独立于注入器本身，避免把 Win32 进程创建细节混进 HybridNtInjectorUtils。
namespace EfmiParentLaunch
{
    class ParentProcessGuard
    {
    public:
        ParentProcessGuard() = default;
        ~ParentProcessGuard() { Reset(); }

        ParentProcessGuard(const ParentProcessGuard &) = delete;
        ParentProcessGuard &operator=(const ParentProcessGuard &) = delete;

        ParentProcessGuard(ParentProcessGuard &&other) noexcept { MoveFrom(other); }
        ParentProcessGuard &operator=(ParentProcessGuard &&other) noexcept
        {
            if (this != &other)
            {
                Reset();
                MoveFrom(other);
            }
            return *this;
        }

        explicit operator bool() const { return process_ != nullptr; }

        void Attach(HANDLE process, HANDLE thread)
        {
            Reset();
            process_ = process;
            thread_ = thread;
        }

        void ReleaseAfter(DWORD delay_ms)
        {
            if (!process_)
                return;
            Sleep(delay_ms);
            Reset();
        }

        void Reset()
        {
            if (process_)
            {
                TerminateProcess(process_, 0);
                CloseHandle(process_);
                process_ = nullptr;
            }
            if (thread_)
            {
                CloseHandle(thread_);
                thread_ = nullptr;
            }
        }

    private:
        void MoveFrom(ParentProcessGuard &other)
        {
            process_ = other.process_;
            thread_ = other.thread_;
            other.process_ = nullptr;
            other.thread_ = nullptr;
        }

        HANDLE process_ = nullptr;
        HANDLE thread_ = nullptr;
    };

    struct LaunchResult
    {
        bool ok = false;
        DWORD target_pid = 0;
        std::string error;
    };

    namespace Detail
    {
        inline std::vector<wchar_t> ToWide(const std::wstring &value)
        {
            std::vector<wchar_t> buffer(value.begin(), value.end());
            buffer.push_back(L'\0');
            return buffer;
        }

        inline std::vector<wchar_t> CommandLine(
            const std::filesystem::path &executable,
            const std::wstring &arguments)
        {
            std::wstring line = L"\"" + executable.wstring() + L"\"";
            if (!arguments.empty())
                line += L" " + arguments;
            return ToWide(line);
        }

        inline std::vector<unsigned> ParseVersion(const std::wstring &name)
        {
            std::vector<unsigned> parts;
            size_t start = 0;
            while (start <= name.size())
            {
                const size_t dot = name.find(L'.', start);
                const auto part = name.substr(
                    start,
                    dot == std::wstring::npos ? std::wstring::npos : dot - start);
                try
                {
                    parts.push_back(static_cast<unsigned>(std::stoul(part)));
                }
                catch (...)
                {
                    parts.push_back(0);
                }
                if (dot == std::wstring::npos)
                    break;
                start = dot + 1;
            }
            return parts;
        }

        inline std::filesystem::path ResolveParentLauncher(
            const std::filesystem::path &configured)
        {
            if (_wcsicmp(configured.filename().c_str(), L"Games.exe") == 0)
                return configured;

            const auto folder = configured.parent_path();
            std::vector<std::pair<std::vector<unsigned>, std::filesystem::path>> candidates;
            std::error_code error;
            for (const auto &entry : std::filesystem::directory_iterator(folder, error))
            {
                if (!entry.is_directory())
                    continue;
                const auto games = entry.path() / L"Games.exe";
                if (!std::filesystem::is_regular_file(games, error))
                    continue;
                candidates.emplace_back(
                    ParseVersion(entry.path().filename().wstring()),
                    games);
            }
            if (candidates.empty())
                return {};

            const auto latest = std::max_element(
                candidates.begin(),
                candidates.end(),
                [](const auto &left, const auto &right)
                {
                    return left.first < right.first;
                });
            return latest->second;
        }

        inline void StopLauncherProcesses()
        {
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE)
                return;

            PROCESSENTRY32W entry{sizeof(entry)};
            if (Process32FirstW(snapshot, &entry))
            {
                do
                {
                    if (_wcsicmp(entry.szExeFile, L"Games.exe") != 0)
                        continue;
                    HANDLE process = OpenProcess(
                        PROCESS_TERMINATE | SYNCHRONIZE,
                        FALSE,
                        entry.th32ProcessID);
                    if (!process)
                        continue;
                    TerminateProcess(process, 0);
                    WaitForSingleObject(process, 5000);
                    CloseHandle(process);
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
            Sleep(300);
        }

        inline bool StartSuspended(
            const std::filesystem::path &executable,
            const std::wstring &arguments,
            PROCESS_INFORMATION &process_info,
            std::string &error)
        {
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            auto command = CommandLine(executable, arguments);
            auto path = ToWide(executable.wstring());
            auto work_dir = ToWide(executable.parent_path().wstring());
            if (!CreateProcessW(
                    path.data(),
                    command.data(),
                    nullptr,
                    nullptr,
                    FALSE,
                    CREATE_SUSPENDED | CREATE_NO_WINDOW,
                    nullptr,
                    work_dir.data(),
                    &startup,
                    &process_info))
            {
                error = "CreateProcessW failed for " + executable.string() +
                    ": " + std::to_string(GetLastError());
                return false;
            }
            return true;
        }

        inline bool StartWithParent(
            const std::filesystem::path &executable,
            const std::wstring &arguments,
            HANDLE parent,
            PROCESS_INFORMATION &process_info,
            std::string &error)
        {
            SIZE_T attribute_size = 0;
            InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
            if (!attribute_size)
            {
                error = "Could not size parent attribute list: " +
                    std::to_string(GetLastError());
                return false;
            }

            std::vector<unsigned char> attribute_buffer(attribute_size);
            auto attribute_list =
                reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_buffer.data());
            if (!InitializeProcThreadAttributeList(attribute_list, 1, 0, &attribute_size))
            {
                error = "Could not initialize parent attribute list: " +
                    std::to_string(GetLastError());
                return false;
            }

            HANDLE parent_handle = parent;
            if (!UpdateProcThreadAttribute(
                    attribute_list,
                    0,
                    PROC_THREAD_ATTRIBUTE_PARENT_PROCESS,
                    &parent_handle,
                    sizeof(parent_handle),
                    nullptr,
                    nullptr))
            {
                DeleteProcThreadAttributeList(attribute_list);
                error = "Could not set parent attribute: " +
                    std::to_string(GetLastError());
                return false;
            }

            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.lpAttributeList = attribute_list;
            auto command = CommandLine(executable, arguments);
            auto path = ToWide(executable.wstring());
            auto work_dir = ToWide(executable.parent_path().wstring());
            const BOOL created = CreateProcessW(
                path.data(),
                command.data(),
                nullptr,
                nullptr,
                FALSE,
                EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW,
                nullptr,
                work_dir.data(),
                &startup.StartupInfo,
                &process_info);
            DeleteProcThreadAttributeList(attribute_list);
            if (!created)
            {
                error = "CreateProcessW failed for " + executable.string() +
                    ": " + std::to_string(GetLastError());
                return false;
            }
            return true;
        }
    } // namespace Detail

    inline LaunchResult Launch(
        const std::filesystem::path &configured_launcher,
        const std::filesystem::path &target,
        const std::wstring &target_arguments,
        ParentProcessGuard &parent_guard)
    {
        LaunchResult result;
        const auto parent = Detail::ResolveParentLauncher(configured_launcher);
        if (parent.empty())
        {
            result.error = "Games.exe not found under launcher folder: " +
                configured_launcher.string();
            return result;
        }

        Detail::StopLauncherProcesses();

        PROCESS_INFORMATION parent_info{};
        if (!Detail::StartSuspended(parent, L"", parent_info, result.error))
            return result;

        ParentProcessGuard local_parent;
        local_parent.Attach(parent_info.hProcess, parent_info.hThread);

        PROCESS_INFORMATION target_info{};
        if (!Detail::StartWithParent(
                target,
                target_arguments,
                parent_info.hProcess,
                target_info,
                result.error))
            return result;

        if (ResumeThread(target_info.hThread) == static_cast<DWORD>(-1))
        {
            result.error = "ResumeThread failed: " +
                std::to_string(GetLastError());
            TerminateProcess(target_info.hProcess, 0);
            CloseHandle(target_info.hThread);
            CloseHandle(target_info.hProcess);
            return result;
        }

        CloseHandle(target_info.hThread);
        CloseHandle(target_info.hProcess);
        parent_guard = std::move(local_parent);
        result.ok = true;
        result.target_pid = target_info.dwProcessId;
        printf(
            "[Loader] EFMI parent: %S (PID=%lu), target PID=%lu\n",
            parent.c_str(),
            parent_info.dwProcessId,
            result.target_pid);
        return result;
    }
} // namespace EfmiParentLaunch
