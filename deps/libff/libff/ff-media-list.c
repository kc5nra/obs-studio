#include "ff-media-list.h"

#include <libavutil/mem.h>
#include <stdbool.h>
#include <time.h>

static struct ff_media_item *media_item_create(const char *media_location)
{
	struct ff_media_item *item = av_mallocz(sizeof(struct ff_media_item));
	item->media_location = av_strdup(media_location);
	return item;
}

static void media_item_free(struct ff_media_item *media_item)
{
	assert(media_item != NULL);

	if (media_item->media_location != NULL)
		av_free(media_item->media_location);

	av_free(media_item);
}

static void list_shuffle(struct ff_list *list)
{
	assert(list != NULL);

	srand(time(NULL));

	if (list->size > 1) {
		size_t list_size = list->size;
		size_t i;
		struct ff_media_item **items;
		struct ff_media_item *item;

		items = av_malloc_array(sizeof(struct ff_media_item *),
				list_size);

		if (items == NULL) {
			av_log(NULL, AV_LOG_ERROR, "unable to allocate memory"
					" for media list shuffle array");
			return;
		}

		item = ff_list_pop_front(list);
		for(i = 0; i < list_size; i++) {
			items[i] = item;
			item = ff_list_pop_front(list);
		}

		for(i = 0; i < list_size - 1; i++) {
			int j = rand() / (RAND_MAX / (list_size - 1) + 1);
			item = items[j];
			items[j] = items[i];
			items[i] = item;
		}

		for(i = 0; i < list_size; i++) {
			ff_list_push_back(list, items[i]);
		}
	}
}

static void list_clear(struct ff_list *list)
{
	struct ff_media_item *item =
			ff_list_pop_front(list);
	while(item != NULL) {
		av_free(item);
		item = ff_list_pop_front(list);
	}
}

static void list_add_all(struct ff_list *from, struct ff_list *to)
{
	const struct ff_list_node *node = from->first;
	while(node != NULL) {
		ff_list_push_back(to, node->value);
		node = node->next;
	}
}

bool ff_media_list_init(struct ff_media_list *media_list)
{
	ff_list_init(&media_list->media_items);
	ff_list_init(&media_list->ordered_media_items);

	return pthread_mutex_init(&media_list->lock, NULL) != 0;
}

void ff_media_list_free(struct ff_media_list *media_list)
{
	// We only clear the media_items as the ordered list is a shallow copy
	list_clear(&media_list->media_items);
	ff_list_free(&media_list->media_items);
	ff_list_free(&media_list->ordered_media_items);
}

void ff_media_list_lock(struct ff_media_list *media_list)
{
	pthread_mutex_lock(&media_list->lock);
}

void ff_media_list_unlock(struct ff_media_list *media_list)
{
	pthread_mutex_unlock(&media_list->lock);
}

void ff_media_list_add(struct ff_media_list *media_list, char *media_location)
{
	struct ff_media_item *item = media_item_create(media_location);
	ff_list_push_back(&media_list->media_items, item);
	ff_list_push_back(&media_list->ordered_media_items, item);
	list_shuffle(&media_list->ordered_media_items);
	media_list->current_node = media_list->ordered_media_items.first;
}

const struct ff_media_item *ff_media_list_current(
		const struct ff_media_list *media_list)
{
	return media_list->current_node->value;
}

const struct ff_media_item *ff_media_list_next(
		struct ff_media_list *media_list)
{
	if (media_list->current_node == NULL ||
			media_list->current_node->next == NULL)
		return NULL;

	media_list->current_node = media_list->current_node->next;
	return media_list->current_node->value;
}

const struct ff_media_item *ff_media_list_previous(
		struct ff_media_list *media_list)
{
	if (media_list->current_node == NULL ||
			media_list->current_node->prev == NULL)
		return NULL;

	media_list->current_node = media_list->current_node->prev;
	return media_list->current_node->value;
}
