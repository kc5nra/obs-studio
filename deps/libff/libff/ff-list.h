#pragma once

#include <libavutil/mem.h>
#include <assert.h>
#include <stddef.h>


struct ff_list {
	struct ff_list_node *first;
	struct ff_list_node *last;
	size_t size;
};

struct ff_list_node {
	const void *value;
	struct ff_list_node *next;
	struct ff_list_node *prev;
};

static inline void ff_list_init(struct ff_list *list)
{
	assert(list);

	list->first = NULL;
	list->last = NULL;
	list->size = 0;
}

static inline void ff_list_free(struct ff_list *list)
{
	assert(list);
	struct ff_list_node *node = list->first;
	while (node != NULL) {
		struct ff_list_node *next = node->next;
		av_free(node);
		node = next;
	}

	list->first = NULL;
	list->last = NULL;
	list->size = 0;
}

static inline void ff_list_push_front(struct ff_list *list, const void *value)
{
	assert(list != NULL);

	struct ff_list_node *new_node = av_mallocz(sizeof(struct ff_list_node));

	new_node->prev = NULL;

	if (list->first != NULL) {
		list->first->prev = new_node;
		new_node->next = list->first;
	} else {
		list->first = list->last = new_node;
		new_node->next = NULL;
	}

	new_node->value = value;
	list->first = new_node;
	list->size++;
}

static inline void ff_list_push_back(struct ff_list *list, const void *value)
{
	assert(list != NULL);

	struct ff_list_node *new_node = av_mallocz(sizeof(struct ff_list_node));
	new_node->value = value;
	new_node->prev = list->last;

	if (list->last)
		list->last->next = new_node;
	else
		list->first = new_node;

	list->last = new_node;
	list->size++;
}

static inline void *ff_list_pop_front(struct ff_list *list)
{
	assert(list != NULL);

	if (list->first == NULL)
		return NULL;

	const void *value = list->first->value;

	if (list->first == list->last) {
		av_free(list->first);
		list->first = list->last = NULL;
	} else {
		struct ff_list_node *new_first = list->first->next;
		av_free(list->first);
		list->first = new_first;
		list->first->prev = NULL;
	}

	list->size--;
	return (void *)value;
}

static inline void *ff_list_pop_back(struct ff_list *list)
{
	assert(list != NULL);

	if (list->last == NULL)
		return NULL;

	const void *value = list->last->value;

	if (list->first == list->last) {
		av_free(list->last);
		list->first = list->last = NULL;
	} else {
		struct ff_list_node *new_last = list->last->prev;
		av_free(list->last);
		list->last = new_last;
		list->last->next = NULL;
	}

	list->size--;
	return (void *)value;
}
