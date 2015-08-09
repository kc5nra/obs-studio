#include <obs-module.h>
#include <mfapi.h>
extern void RegisterMFAACEncoder();
extern void RegisterMFH264Encoder();

bool obs_module_load(void)
{
	RegisterMFAACEncoder();
	RegisterMFH264Encoder();

	return true;
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-mf", "en-US")
