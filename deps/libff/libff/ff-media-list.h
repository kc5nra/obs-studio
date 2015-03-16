#pragma once

#include "ff-list.h"

#include <stdbool.h>
#include <pthread.h>

struct ff_media_item {
	char *media_location;
};

struct ff_media_list {
	struct ff_list media_items;
	pthread_mutex_t lock;
};
bool ff_media_list_init(struct ff_media_list *media_list);
void ff_media_list_free(struct ff_media_list *media_list);
void ff_media_list_lock(struct ff_media_list *media_list);
void ff_media_list_unlock(struct ff_media_list *media_list);

void ff_media_list_add(struct ff_media_list *media_list, char *media_location);
