/*
obs-ivs
Copyright (C) 2023-2023 John R. Bradley <jocbrad@twitch.tv>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <QAction>
#include <QMainWindow>
#include <obs-module.h>
#include <obs-frontend-api.h>

#include "obs-ivs.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-ivs", "en-US")
OBS_MODULE_AUTHOR("Twitch/IVS")
const char *obs_module_name(void)
{
	return "obs-ivs";
}
const char *obs_module_description(void)
{
	return obs_module_text("OVSIVS.Plugin.Description");
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "Loading module obs-ivs (%s)", OBS_IVS_VERSION);
	return true;
}

void obs_module_unload()
{
	blog(LOG_INFO, "Unloading module");
}
