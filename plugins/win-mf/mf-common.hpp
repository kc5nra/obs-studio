#pragma once

#define MF_LOG(level, format, ...) \
	blog(level, "[Media Foundation encoder]: " format, ##__VA_ARGS__)
#define MF_LOG_ENCODER(format_name, encoder, level, format, ...) \
	blog(level, "[Media Foundation %s: '%s']: " format, \
			format_name, obs_encoder_get_name(encoder), \
			##__VA_ARGS__)

namespace MF {
enum Status {
	FAILURE,
	SUCCESS,
	NOT_ACCEPTING,
	NEED_MORE_INPUT
};
}
