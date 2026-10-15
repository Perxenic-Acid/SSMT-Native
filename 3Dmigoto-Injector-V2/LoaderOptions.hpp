#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <utility>

struct LoaderOptions
{
    std::optional<std::filesystem::path>
        plugin_host_config;
    bool machine_readable = false;
    bool test_mode = false;
    bool preload_runtime = false;
    bool direct_injection = false;
    bool wait_for_target = false;
    std::vector<std::pair<std::wstring, std::optional<std::wstring>>> environment;
    unsigned long process_timeout_ms = 120000;
    std::optional<std::wstring> launch_barrier_id;
};

inline LoaderOptions ParseLoaderOptions(
    int argc,
    wchar_t *argv[])
{
    LoaderOptions options;

    for (int i = 1; i < argc; ++i)
    {
        const std::wstring_view arg{
            argv[i]};

        if (arg == L"--set-env" || arg == L"--unset-env")
        {
            if (i + 1 >= argc)
                throw std::runtime_error("--set-env/--unset-env requires a name");
            std::wstring value{argv[++i]};
            const auto separator = value.find(L'=');
            const auto name = arg == L"--set-env" ? value.substr(0, separator) : value;
            if (name.empty() || name.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") != std::wstring::npos ||
                (arg == L"--set-env" && separator == std::wstring::npos))
                throw std::runtime_error("Invalid environment name or assignment");
            options.environment.emplace_back(name, arg == L"--set-env" ? std::optional<std::wstring>{value.substr(separator + 1)} : std::nullopt);
            continue;
        }

        if (arg == L"--plugin-host-config")
        {
            if (i + 1 >= argc)
                throw std::runtime_error(
                    "--plugin-host-config requires a path");

            options.plugin_host_config =
                std::filesystem::path{
                    argv[++i]};

            continue;
        }

        if (arg == L"--machine-readable" || arg == L"--events")
        {
            if (arg == L"--events")
            {
                if (i + 1 >= argc || std::wstring_view{argv[i + 1]} != L"jsonl")
                    throw std::runtime_error(
                        "--events requires jsonl");
                ++i;
            }

            options.machine_readable = true;
            continue;
        }

        if (arg == L"--test-mode")
        {
            options.test_mode = true;
            continue;
        }
        if (arg == L"--inject-mode")
        {
            if (i + 1 >= argc)
                throw std::runtime_error("--inject-mode requires hook or direct");
            const std::wstring_view mode{argv[++i]};
            if (mode != L"hook" && mode != L"direct")
                throw std::runtime_error("--inject-mode requires hook or direct");
            options.direct_injection = mode == L"direct";
            continue;
        }
        if (arg == L"--wait-for-target")
        {
            options.wait_for_target = true;
            continue;
        }
        if (arg == L"--process-timeout")
        {
            if (i + 1 >= argc)
                throw std::runtime_error("--process-timeout requires seconds (1..3600)");
            const std::wstring_view value{argv[++i]};
            unsigned long seconds = 0;
            for (const wchar_t digit : value)
            {
                if (digit < L'0' || digit > L'9' || seconds > 3600)
                    throw std::runtime_error("--process-timeout requires seconds (1..3600)");
                seconds = seconds * 10 + (digit - L'0');
            }
            if (seconds == 0 || seconds > 3600)
                throw std::runtime_error("--process-timeout requires seconds (1..3600)");
            options.process_timeout_ms = seconds * 1000;
            continue;
        }
        if (arg == L"--preload-runtime")
        {
            options.preload_runtime = true;
            continue;
        }

        if (arg == L"--launch-barrier")
        {
            if (i + 1 >= argc)
                throw std::runtime_error("--launch-barrier requires an id");
            options.launch_barrier_id = argv[++i];
            continue;
        }
        throw std::runtime_error(
            "Unknown command-line argument");
    }

    if (!options.environment.empty() && options.direct_injection)
        throw std::runtime_error("Environment requires a newly launched target");
    return options;
}
