#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <string_view>

#include "D3dxIniUtils.hpp"
#include "HybridNtInjectorUtils.hpp"
#include "LaunchEvents.hpp"
#include "LoaderOptions.hpp"

int wmain(
	int argc,
	wchar_t *argv[])
{
	bool machineRequested = false;
	for (int i = 1; i < argc; ++i)
	{
		if (std::wstring_view{argv[i]} == L"--machine-readable" ||
			(std::wstring_view{argv[i]} == L"--events" && i + 1 < argc && std::wstring_view{argv[i + 1]} == L"jsonl"))
		{
			machineRequested = true;
			break;
		}
	}
	HANDLE machine_output = GetStdHandle(STD_OUTPUT_HANDLE);
	if (machineRequested)
	{
		// 保留 CRT 已重定向的 stdout 句柄，随后把人工日志移到 stderr。
		// Start-Process 的文件重定向不保证 GetStdHandle 与 CRT 指向同一对象。
		const auto stdout_handle = _get_osfhandle(_fileno(stdout));
		if (stdout_handle != -1)
		{
			HANDLE duplicate = nullptr;
			if (DuplicateHandle(GetCurrentProcess(), reinterpret_cast<HANDLE>(stdout_handle),
				GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS))
				machine_output = duplicate;
		}
		_dup2(_fileno(stderr), _fileno(stdout));
	}
	LaunchEventEmitter events(machineRequested, machine_output);

	LoaderOptions options;

	try
	{
		options =
			ParseLoaderOptions(
				argc,
				argv);
	}
	catch (const std::exception &error)
	{
		events.emit_error("arguments", "invalid_arguments", error.what());
		printf(
			"ERROR: %s\n",
			error.what());

		return EXIT_FAILURE;
	}
	if (options.test_mode)
	{
		events.emit("launch_started");
		if (options.plugin_host_config && !std::filesystem::exists(*options.plugin_host_config))
		{
			events.emit_error("preflight", "plugin_host_config_missing", "PluginHost config does not exist");
			return EXIT_FAILURE;
		}
		events.emit_pid("target_created", 0);
		events.emit_module("inject_begin", L"SSMT-Player-Tweaks.dll");
		events.emit_module("inject_complete", L"SSMT-Player-Tweaks.dll");
		if (options.plugin_host_config)
		{
			events.emit("plugin_host_begin");
			events.emit("plugin_host_ready");
		}
		else
		{
			events.emit("plugin_host_disabled");
		}
		events.emit("before_resume");
		events.emit("target_resumed");
		events.emit("runtime_detected");
		events.emit("launch_complete");
		return EXIT_SUCCESS;
	}
	events.emit("launch_started");

	HANDLE instanceMutex = CreateMutexA(nullptr, FALSE, "Local\\3DMigotoLoader");
	if (!instanceMutex)
	{
		events.emit_error("preflight", "loader_mutex_failed", "Failed to create loader mutex");
		printf("ERROR: Failed to create loader mutex: %lu\n", GetLastError());
		return EXIT_FAILURE;
	}

	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		events.emit_error("preflight", "loader_already_running", "Another loader instance is already running");
		printf("ERROR: Another instance of the 3DMigoto Loader is already running. Please close it and try again\n");
		CloseHandle(instanceMutex);
		return EXIT_FAILURE;
	}

	printf("\n------------------------------- 3DMigoto Loader V3-001 ------------------------------\n\n");
	printf("This program is modified by NicoMico based on the 3Dmigoto source code and is exclusively included in the SSMT Release for free distribution. It is completely free! If you paid any amount of money to obtain it, you have definitely been scammed!\n\n");
	printf("\n----------------------------------------------------------------------------------\n\n");
	D3dxIniUtils d3dxIniUtils(L"d3dx.ini");

	if (d3dxIniUtils.parse_error != L"")
	{
		events.emit_error("configuration", "d3dx_ini_invalid", "Failed to parse d3dx.ini");
		printf("%s", d3dxIniUtils.ToByteString(d3dxIniUtils.parse_error).c_str());
		CloseHandle(instanceMutex);
		return EXIT_FAILURE;
	}

	if (options.plugin_host_config)
	{
		const auto &config =
			*options.plugin_host_config;

		if (!std::filesystem::exists(config))
		{
			events.emit_error("preflight", "plugin_host_config_missing", "PluginHost config does not exist");
			printf(
				"ERROR: PluginHost config does not exist: %S\n",
				config.c_str());

			CloseHandle(instanceMutex);
			return EXIT_FAILURE;
		}

		printf(
			"[Loader] PluginHost enabled.\n"
			"[Loader] Config: %S\n",
			config.c_str());
	}
	else
	{
		events.emit("plugin_host_disabled");
		printf(
			"[Loader] PluginHost disabled.\n");
	}

	const bool ok = HybridNtInjectorUtils::Run(d3dxIniUtils, options, machine_output);

	CloseHandle(instanceMutex);

	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
