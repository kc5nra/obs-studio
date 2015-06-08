#include <obs-module.h>

extern void RegisterMFH264Encoder();
extern void RegisterMFAACEncoder();

bool obs_module_load(void)
{
	RegisterMFH264Encoder();
	RegisterMFAACEncoder();
	return true;
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-mf", "en-US")
