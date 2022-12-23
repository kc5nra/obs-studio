cmake_minimum_required(VERSION 3.16...3.25)

find_qt(COMPONENTS Core Widgets Svg Network)

add_library(ivs MODULE)
add_library(OBS::ivs ALIAS ivs)

target_sources(ivs PRIVATE)

target_sources(ivs PRIVATE src/obs-ivs.cpp)

configure_file(src/plugin-macros.h.in plugin-macros.generated.h)
target_sources(ivs PRIVATE plugin-macros.generated.h)

target_link_libraries(ivs PRIVATE OBS::libobs OBS::frontend-api Qt::Core Qt::Widgets Qt::Svg Qt::Network)

set_target_properties_obs(
  ivs
  PROPERTIES FOLDER plugins
             PREFIX ""
             AUTOMOC ON
             AUTOUIC ON
             AUTORCC ON)

if(OS_WINDOWS)
  set_property(
    TARGET ivs
    APPEND
    PROPERTY AUTORCC_OPTIONS --format-version 1)
endif()
