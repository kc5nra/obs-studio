#pragma once

#include "ff-demuxer.h"
#include "ff-media-list.h"

struct ff_player {
	struct ff_demuxer active_demuxer;
	struct ff_demuxer staged_demuxer;
	const struct ff_media_list *media_list;
};

typedef struct ff_player ff_player_t;

bool ff_player_init(struct ff_player *player,
		const struct ff_media_list *media_list);
