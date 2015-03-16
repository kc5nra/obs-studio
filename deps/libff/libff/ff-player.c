#include "ff-player.h"

#include <libavutil/threadmessage.h>
bool ff_player_init(struct ff_player *player,
		const struct ff_media_list *media_list)
{
	assert(media_list != NULL);

	player->media_list = media_list;
}

bool ff_player_start(struct ff_player *player)
{
	ff_me
}
