#include "whip-stages.h"

#include <regex>
#include <unordered_map>
#include <sstream>

#include "base64/base64.hpp"

namespace stages {

// function to parse the ICE link header and extract the URL, username, and credential attributes
std::unordered_map<std::string, std::string>
parse_ice_link_header(const std::string &ice_link_header)
{
	std::unordered_map<std::string, std::string> ice_attrs;
	std::regex ice_regex(
		R"(<([^>]+)>;.*username=([^;]+);.*credential=([^;]+);.*)");

	// search for the ICE attribute and URL in the link header
	std::smatch ice_match;
	if (std::regex_match(ice_link_header, ice_match, ice_regex)) {
		ice_attrs["url"] = ice_match[1];
		ice_attrs["username"] = ice_match[2];
		ice_attrs["credential"] = ice_match[3];
	}

	return ice_attrs;
}

std::vector<std::string> split_jwt(const std::string &token)
{
	// Split the token into its three parts
	std::vector<std::string> parts;
	std::string::size_type pos1 = 0, pos2 = 0;
	while ((pos2 = token.find('.', pos1)) != std::string::npos) {
		parts.push_back(token.substr(pos1, pos2 - pos1));
		pos1 = pos2 + 1;
	}
	parts.push_back(token.substr(pos1));

	// Base64 decode each part
	for (auto &part : parts) {
		part = base64_decode(part);
	}

	return parts;
}

std::tuple<bool, std::string, std::string>
parse_header_line(const std::string &header)
{
	size_t colon_pos = header.find(":");
	if (colon_pos != std::string::npos) {
		std::string key = header.substr(0, colon_pos);
		std::string value = header.substr(colon_pos + 1);
		// remove leading and trailing whitespace from the value
		value.erase(0, value.find_first_not_of(" \t\r\n"));
		value.erase(value.find_last_not_of(" \t\r\n") + 1);
		return std::make_tuple(true, key, value);
	}

	return std::make_tuple(false, std::string(), std::string());
}

size_t handle_options_headers(char *buffer, size_t size, size_t nitems,
			      void *userdata)
{

	auto headers =
		static_cast<std::unordered_map<std::string, std::string> *>(
			userdata);

	size_t real_size = size * nitems;

	if (real_size > 0) {
		// Extract the header from the buffer
		std::string header(buffer, real_size);

		if (header.find("HTTP/") == 0) {
			// Clear the headers from previous redirects
			headers->clear();
		} else {
			bool is_header = false;
			std::string key;
			std::string value;
			std::tie(is_header, key, value) =
				parse_header_line(header);
			if (is_header) {
				headers->emplace(key, value);
			}
		}
	}

	return real_size;
}

std::vector<std::string> get_ice_servers(std::string url, std::string bearer)
{
	struct curl_slist *headers = NULL;
	if (!bearer.empty()) {
		auto bearer_token_header =
			std::string("Authorization: Bearer ") + bearer;
		headers =
			curl_slist_append(headers, bearer_token_header.c_str());
	}

	std::unordered_map<std::string, std::string> response_headers;

	std::vector<std::string> ice_servers;

	CURL *c = curl_easy_init();
	curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, handle_options_headers);
	curl_easy_setopt(c, CURLOPT_HEADERDATA, (void *)&response_headers);
	curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(c, CURLOPT_URL, url.c_str());
	curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "OPTIONS");
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	// This is required since redirets change host with IVS stages
	curl_easy_setopt(c, CURLOPT_UNRESTRICTED_AUTH, 1L);
	curl_easy_setopt(c, CURLOPT_HTTPAUTH, CURLAUTH_BEARER);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 8L);

	auto cleanup = [&]() {
		curl_easy_cleanup(c);
		curl_slist_free_all(headers);
	};

	CURLcode res = curl_easy_perform(c);
	if (res != CURLE_OK) {
		blog(LOG_WARNING,
		     "OPTIONS request failed: CURL returned result not CURLE_OK");
		cleanup();
		return ice_servers;
	}
	long response_code;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &response_code);
	if (response_code != 200) {
		blog(LOG_WARNING,
		     "Connect failed: HTTP endpoint returned response code %ld",
		     response_code);
		cleanup();
		return ice_servers;
	}

	auto link_header = response_headers.find("Link");
	if (link_header != response_headers.end()) {
		auto link_headers = parse_ice_link_header(link_header->second);
		ice_servers.push_back("test");
	}

	cleanup();
	return ice_servers;
}

std::tuple<bool, std::string, std::string>
get_connection_info(const std::string &bearer_token)
{
	auto parts = split_jwt(bearer_token);
	if (parts.size() == 3) {
		// This is most likely a jwt
		// We don't care about signature and header since these are validated on the server
		auto payload = parts[1];
		obs_data_t *data = obs_data_create_from_json(payload.c_str());
		const char *whip_url_cstr =
			obs_data_get_string(data, "whip_url");
		const char *jti_cstr = obs_data_get_string(data, "jti");
		if (whip_url_cstr && jti_cstr && *whip_url_cstr && *jti_cstr)
			return std::make_tuple(true,
					       std::string(whip_url_cstr) +
						       "/publish/" +
						       std::string(jti_cstr),
					       bearer_token);
	}

	return std::make_tuple(false, std::string(), std::string());
}

}
