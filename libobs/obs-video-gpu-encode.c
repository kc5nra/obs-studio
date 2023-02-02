/******************************************************************************
    Copyright (C) 2018 by Hugh Bailey <obs.jim@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "obs-internal.h"

static const char *scale_input_texture_name = "scale_input_texture";
static inline struct obs_tex_frame *
scale_input_texture(struct obs_core_video_mix *mix,
		    struct obs_tex_frame *input_texture,
		    struct obs_tex_frame *target)
{
	struct obs_core_video *video = &obs->video;
	//gs_texture_t *texture = mix->render_texture;
	//gs_texture_t *target = mix->output_texture;
	uint32_t input_width = gs_texture_get_width(input_texture->tex);
	uint32_t input_height = gs_texture_get_height(input_texture->tex);
	uint32_t width = gs_texture_get_width(target->tex);
	uint32_t height = gs_texture_get_height(target->tex);
	gs_effect_t *effect = video->bicubic_effect;
	gs_technique_t *tech;

	/* if the dimension is under half the size of the original image,
	 * bicubic/lanczos can't sample enough pixels to create an accurate
	 * image, so use the bilinear low resolution effect instead */
	if (width < (input_width / 2) && height < (input_height / 2)) {
		effect = video->bilinear_lowres_effect;
	}

	if (video_output_get_format(mix->video) == VIDEO_FORMAT_BGRA) {
		tech = gs_effect_get_technique(effect, "DrawAlphaDivide");
	} else {
		if ((width == gs_texture_get_width(input_texture->tex)) &&
		    (height == gs_texture_get_height(input_texture->tex)))
			return input_texture;

		tech = gs_effect_get_technique(effect, "Draw");
	}

	profile_start(scale_input_texture_name);

	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	gs_eparam_t *bres =
		gs_effect_get_param_by_name(effect, "base_dimension");
	gs_eparam_t *bres_i =
		gs_effect_get_param_by_name(effect, "base_dimension_i");
	size_t passes, i;

	gs_set_render_target(target->tex, NULL);

	//set_render_size(width, height);
	gs_enable_depth_test(false);
	gs_set_cull_mode(GS_NEITHER);

	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);
	gs_set_viewport(0, 0, width, height);

	if (bres) {
		struct vec2 base;
		vec2_set(&base, (float)mix->ovi.base_width,
			 (float)mix->ovi.base_height);
		gs_effect_set_vec2(bres, &base);
	}

	if (bres_i) {
		struct vec2 base_i;
		vec2_set(&base_i, 1.0f / (float)mix->ovi.base_width,
			 1.0f / (float)mix->ovi.base_height);
		gs_effect_set_vec2(bres_i, &base_i);
	}

	gs_effect_set_texture_srgb(image, input_texture->tex);

	gs_enable_framebuffer_srgb(true);
	gs_enable_blending(false);
	passes = gs_technique_begin(tech);
	for (i = 0; i < passes; i++) {
		gs_technique_begin_pass(tech, i);
		gs_draw_sprite(input_texture->tex, 0, width, height);
		gs_technique_end_pass(tech);
	}
	gs_technique_end(tech);
	gs_enable_blending(true);
	gs_enable_framebuffer_srgb(false);

	profile_end(scale_input_texture_name);

	return target;
}

static void *gpu_encode_thread(struct obs_core_video_mix *video)
{
	uint64_t interval = video_output_get_frame_time(video->video);
	DARRAY(obs_encoder_t *) encoders;
	int wait_frames = NUM_ENCODE_TEXTURE_FRAMES_TO_WAIT;

	da_init(encoders);

	os_set_thread_name("obs gpu encode thread");

	while (os_sem_wait(video->gpu_encode_semaphore) == 0) {
		struct obs_tex_frame tf;
		uint64_t timestamp;
		uint64_t lock_key;
		uint64_t next_key;
		uint64_t next_input_key;
		size_t lock_count = 0;

		if (os_atomic_load_bool(&video->gpu_encode_stop))
			break;

		if (wait_frames) {
			wait_frames--;
			continue;
		}

		os_event_reset(video->gpu_encode_inactive);

		/* -------------- */
		const struct video_output_info *info =
			video_output_get_info(video->video);

		pthread_mutex_lock(&video->gpu_encoder_mutex);

		circlebuf_pop_front(&video->gpu_encoder_queue, &tf, sizeof(tf));
		timestamp = tf.timestamp;
		lock_key = tf.lock_key;
		next_input_key = tf.lock_key;

		video_output_inc_texture_frames(video->video);

		for (size_t i = 0; i < video->gpu_encoders.num; i++) {
			obs_encoder_t *encoder = obs_encoder_get_ref(
				video->gpu_encoders.array[i]);
			if (encoder)
				da_push_back(encoders, &encoder);
		}

		pthread_mutex_unlock(&video->gpu_encoder_mutex);

		/* -------------- */

		for (size_t i = 0; i < encoders.num; i++) {
			struct encoder_packet pkt = {0};
			bool received = false;
			bool success;

			obs_encoder_t *encoder = encoders.array[i];
			struct obs_encoder *pair = encoder->paired_encoder;

			pkt.timebase_num = encoder->timebase_num;
			pkt.timebase_den = encoder->timebase_den;
			pkt.encoder = encoder;

			if (!encoder->first_received && pair) {
				if (!pair->first_received ||
				    pair->first_raw_ts > timestamp) {
					continue;
				}
			}

			if (video_pause_check(&encoder->pause, timestamp))
				continue;

			if (encoder->reconfigure_requested) {
				encoder->reconfigure_requested = false;
				encoder->info.update(encoder->context.data,
						     encoder->context.settings);
			}

			// HACK!!! Scale input frame to encoder's desired width/height if needed
			struct obs_tex_frame *input = &tf;
			bool scaled = false;

			if (encoder->scaled_input.tex != NULL &&
				info->width != obs_encoder_get_width(encoder) &&
				info->height != obs_encoder_get_height(encoder)) {

				obs_enter_graphics();
				//pthread_mutex_lock(&video->gpu_encoder_mutex);
				input = scale_input_texture(video, &tf, &encoder->scaled_input);
				//pthread_mutex_unlock(&video->gpu_encoder_mutex);
				obs_leave_graphics();

				scaled = true;
				lock_key = input->lock_key;
				next_key = !input->lock_key;
			} else {
				lock_key = next_input_key;
				next_input_key++;
				if (next_input_key >= encoders.num)
					next_input_key = 0;
				next_key = next_input_key;
			}

			if (!encoder->start_ts)
				encoder->start_ts = timestamp;

			success = encoder->info.encode_texture(
				encoder->context.data, input->handle,
				encoder->cur_pts, lock_key, &next_key,
				&pkt,
				&received);

			if (scaled) {
				input->lock_key = next_key;
			}
			send_off_encoder_packet(encoder, success, received,
						&pkt);

			encoder->cur_pts += encoder->timebase_num;
		}

		/* -------------- */

		pthread_mutex_lock(&video->gpu_encoder_mutex);

		tf.lock_key = next_input_key;

		if (--tf.count) {
			tf.timestamp += interval;
			circlebuf_push_front(&video->gpu_encoder_queue, &tf,
					     sizeof(tf));

			video_output_inc_texture_skipped_frames(video->video);
		} else {
			circlebuf_push_back(&video->gpu_encoder_avail_queue,
					    &tf, sizeof(tf));
		}

		pthread_mutex_unlock(&video->gpu_encoder_mutex);

		/* -------------- */

		os_event_signal(video->gpu_encode_inactive);

		for (size_t i = 0; i < encoders.num; i++)
			obs_encoder_release(encoders.array[i]);

		da_resize(encoders, 0);
	}

	da_free(encoders);
	return NULL;
}

bool init_gpu_encoding(struct obs_core_video_mix *video)
{
#ifdef _WIN32
	const struct video_output_info *info =
		video_output_get_info(video->video);

	video->gpu_encode_stop = false;

	circlebuf_reserve(&video->gpu_encoder_avail_queue, NUM_ENCODE_TEXTURES);
	for (size_t i = 0; i < NUM_ENCODE_TEXTURES; i++) {
		gs_texture_t *tex;
		gs_texture_t *tex_uv;

		if (info->format == VIDEO_FORMAT_P010) {
			gs_texture_create_p010(
				&tex, &tex_uv, info->width, info->height,
				GS_RENDER_TARGET | GS_SHARED_KM_TEX);
		} else {
			gs_texture_create_nv12(
				&tex, &tex_uv, info->width, info->height,
				GS_RENDER_TARGET | GS_SHARED_KM_TEX);
		}
		if (!tex) {
			return false;
		}

		uint32_t handle = gs_texture_get_shared_handle(tex);

		struct obs_tex_frame frame = {
			.tex = tex, .tex_uv = tex_uv, .handle = handle};

		circlebuf_push_back(&video->gpu_encoder_avail_queue, &frame,
				    sizeof(frame));
	}

	if (os_sem_init(&video->gpu_encode_semaphore, 0) != 0)
		return false;
	if (os_event_init(&video->gpu_encode_inactive, OS_EVENT_TYPE_MANUAL) !=
	    0)
		return false;
	if (pthread_create(&video->gpu_encode_thread, NULL, gpu_encode_thread,
			   video) != 0)
		return false;

	os_event_signal(video->gpu_encode_inactive);

	video->gpu_encode_thread_initialized = true;
	return true;
#else
	UNUSED_PARAMETER(video);
	return false;
#endif
}

void stop_gpu_encoding_thread(struct obs_core_video_mix *video)
{
	if (video->gpu_encode_thread_initialized) {
		os_atomic_set_bool(&video->gpu_encode_stop, true);
		os_sem_post(video->gpu_encode_semaphore);
		pthread_join(video->gpu_encode_thread, NULL);
		video->gpu_encode_thread_initialized = false;
	}
}

void free_gpu_encoding(struct obs_core_video_mix *video)
{
	if (video->gpu_encode_semaphore) {
		os_sem_destroy(video->gpu_encode_semaphore);
		video->gpu_encode_semaphore = NULL;
	}
	if (video->gpu_encode_inactive) {
		os_event_destroy(video->gpu_encode_inactive);
		video->gpu_encode_inactive = NULL;
	}

#define free_circlebuf(x)                                               \
	do {                                                            \
		while (x.size) {                                        \
			struct obs_tex_frame frame;                     \
			circlebuf_pop_front(&x, &frame, sizeof(frame)); \
			gs_texture_destroy(frame.tex);                  \
			gs_texture_destroy(frame.tex_uv);               \
		}                                                       \
		circlebuf_free(&x);                                     \
	} while (false)

	free_circlebuf(video->gpu_encoder_queue);
	free_circlebuf(video->gpu_encoder_avail_queue);
#undef free_circlebuf
}
