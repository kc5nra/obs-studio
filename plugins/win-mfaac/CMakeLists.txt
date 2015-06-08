project(win-mf)

set(win-mf_SOURCES
	mf-plugin.c
	mf-aac.cpp
	mf-h264.cpp)

add_library(win-mf MODULE
	${win-mf_SOURCES})

target_link_libraries(win-mf
	uuid
	mfplat
	mfuuid
	mf
	wmcodecdspuuid
	libobs)

install_obs_plugin_with_data(win-mf data)
