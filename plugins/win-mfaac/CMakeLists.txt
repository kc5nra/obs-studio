project(win-mf)

set(win-mf_SOURCES
	mf-plugin.c
	mf-aac-encoder.cpp
	mf-aac.cpp
	mf-h264.cpp)

set(win-mf_HEADERS
	mf-aac-encoder.h)

add_library(win-mf MODULE
	${win-mf_SOURCES}
	${win-mf_HEADERS})

target_link_libraries(win-mf
	uuid
	mfplat
	mfuuid
	mf
	wmcodecdspuuid
	libobs)

install_obs_plugin_with_data(win-mf data)
