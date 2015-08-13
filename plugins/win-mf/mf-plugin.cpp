#include <obs-module.h>
#include <util/profiler.h>

#include "mf-common.hpp"

extern "C" extern void RegisterMFAACEncoder();
extern void RegisterMFH264Encoder();


extern "C" bool obs_module_load(void)
{
	CoInitializeEx(0, COINIT_MULTITHREADED);
	MFStartup(MF_VERSION, MFSTARTUP_FULL);

	RegisterMFAACEncoder();
	RegisterMFH264Encoder();

	return true;
}

extern "C" void obs_module_unload(void)
{
	MFShutdown();
	CoUninitialize();
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-mf", "en-US")
