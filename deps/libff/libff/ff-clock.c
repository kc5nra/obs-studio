/*
 * Copyright (c) 2015 John R. Bradley <jrb@turrettech.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "ff-clock.h"

#include <libavutil/avutil.h>
#include <libavutil/time.h>

double ff_get_sync_clock(struct ff_clock *clock)
{
	return clock->sync_clock(clock->opaque);
}

int64_t ff_clock_start_time(struct ff_clock *clock)
{
	int64_t start_time = AV_NOPTS_VALUE;

	pthread_mutex_lock(&clock->mutex);
	if (clock->started)
		start_time = clock->start_time;
	pthread_mutex_unlock(&clock->mutex);

	return start_time;
}

bool ff_clock_start(struct ff_clock *clock, enum ff_av_sync_type sync_type,
		bool *abort)
{
	bool release = false;

	if (clock->sync_type == sync_type && !clock->started) {
		pthread_mutex_lock(&clock->mutex);
		if (!clock->started) {
			clock->start_time = av_gettime();
			clock->started = true;
		}
		pthread_cond_signal(&clock->cond);
		pthread_mutex_unlock(&clock->mutex);
	} else {
		while (!clock->started) {
			bool aborted;
			pthread_mutex_lock(&clock->mutex);
			int64_t current_time = av_gettime() + 100;
			struct timespec sleep_time = {
				.tv_sec =  current_time / AV_TIME_BASE,
				.tv_nsec = (current_time % AV_TIME_BASE) * 1000
			};
			pthread_cond_timedwait(&clock->cond, &clock->mutex,
					&sleep_time);
			// there is no way anyone can signal us
			// since we are the only reference
			if (clock->retain == 1)
				release = true;
			aborted = *abort;
			pthread_mutex_unlock(&clock->mutex);

			if (aborted)
				return false;
		}
	}

	if (release)
		ff_clock_release(&clock);

	return !release;
}

struct ff_clock *ff_clock_init(struct ff_clock *clock)
{
	clock = av_mallocz(sizeof(struct ff_clock));

	if (clock == NULL)
		return NULL;

	if (pthread_mutex_init(&clock->mutex, NULL) != 0)
		goto fail;

	if (pthread_cond_init(&clock->cond, NULL) != 0)
		goto fail1;

	return clock;
fail1:
	pthread_mutex_destroy(&clock->mutex);
fail:
	av_free(clock);

	return NULL;
}

struct ff_clock *ff_clock_retain(struct ff_clock *clock)
{
	pthread_mutex_lock(&clock->mutex);
	clock->retain++;
	pthread_mutex_unlock(&clock->mutex);

	return clock;
}

struct ff_clock *ff_clock_move(struct ff_clock **clock)
{
	struct ff_clock *retained_clock = ff_clock_retain(*clock);
	ff_clock_release(clock);

	return retained_clock;
}

void ff_clock_release(struct ff_clock **clock)
{
	bool release = false;
	pthread_mutex_lock(&(*clock)->mutex);
	(*clock)->retain--;
	release = (*clock)->retain == 0;
	pthread_mutex_unlock(&(*clock)->mutex);

	if (release) {
		pthread_cond_destroy(&(*clock)->cond);
		pthread_mutex_destroy(&(*clock)->mutex);
		av_free(*clock);
	}

	*clock = NULL;
}
