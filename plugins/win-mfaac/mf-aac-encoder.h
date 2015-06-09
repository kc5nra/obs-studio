#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mf_aac_encoder;
typedef struct mf_aac_encoder mf_aac_encoder_t;

typedef enum mf_aac_status {
	MF_AAC_FAILURE,
	MF_AAC_SUCCESS,
	MF_AAC_NOT_ACCEPTING,
	MF_AAC_NEED_MORE_INPUT
} mf_aac_status;

mf_aac_encoder_t *mf_aac_encoder_create(uint32_t bitrate,
		uint32_t channels, uint32_t sampleRate,
		uint32_t bitsPerSample);

void mf_aac_encoder_destroy(mf_aac_encoder_t *encoder);
bool mf_aac_encoder_initialize(mf_aac_encoder_t *encoder);

enum mf_aac_status mf_aac_encoder_input(uint8_t *data, uint32_t linesize,
		uint64_t pts);

#ifdef __cplusplus
}
#endif
