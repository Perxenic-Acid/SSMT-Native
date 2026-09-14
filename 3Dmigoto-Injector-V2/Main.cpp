#include <windows.h>
#include <stdio.h>

#include "D3dxIniUtils.hpp"
#include "HybridNtInjectorUtils.hpp"
#include "LoaderOptions.hpp"

int wmain(
	int argc,
	wchar_t *argv[])
{
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
		printf(
			"ERROR: %s\n",
			error.what());

		return EXIT_FAILURE;
	}

	HANDLE instanceMutex = CreateMutexA(nullptr, FALSE, "Local\\3DMigotoLoader");
	if (!instanceMutex)
	{
		printf("ERROR: Failed to create loader mutex: %lu\n", GetLastError());
		return EXIT_FAILURE;
	}

	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
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
		printf(
			"[Loader] PluginHost disabled.\n");
	}

	const bool ok = HybridNtInjectorUtils::Run(d3dxIniUtils, options);

	CloseHandle(instanceMutex);

	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
