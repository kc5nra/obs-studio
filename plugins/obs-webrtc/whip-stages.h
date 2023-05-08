#pragma once

#include <obs-module.h>
#include <util/curl/curl-helper.h>
#include <util/platform.h>
#include <util/base.h>
#include <string>
#include <tuple>
#include <vector>

namespace stages {
std::tuple<bool, std::string, std::string>
get_connection_info(const std::string &bearer_token);
std::vector<std::string> get_ice_servers(std::string url, std::string bearer);
}
