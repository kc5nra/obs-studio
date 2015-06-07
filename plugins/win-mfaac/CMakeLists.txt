project(obs-mfaac)

set(obs-mfaac_SOURCES
	obs-mfaac.cpp)

add_library(obs-mfaac MODULE
	${obs-mfaac_SOURCES})

target_link_libraries(obs-mfaac
	uuid
	mfplat
	mfuuid
	mf
	wmcodecdspuuid
	libobs)

install_obs_plugin_with_data(obs-mfaac data)
