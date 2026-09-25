#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>

struct LoaderOptions
{
    std::optional<std::filesystem::path>
        plugin_host_config;
    bool machine_readable = false;
    bool test_mode = false;
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
        throw std::runtime_error(
            "Unknown command-line argument");
    }

    return options;
}
