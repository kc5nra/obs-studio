#include <obs-module.h>

#include <memory>

#include "mf-common.h"

extern "C" {
void RegisterMFAACEncoder();
}

struct mfaac_encoder {
	obs_encoder_t *encoder;

	int channels;
	int sampleRate;
	int bitsPerSample;

	ComPtr<IMFTransform> transform;
	ComPtr<IMFSample> outputSample;

	int frameSizeBytes;

	std::unique_ptr<BYTE> packetBuffer;
	int packetBufferSize;

	uint8_t header[5];
};

static const char *mfaac_getname(void)
{
	return obs_module_text("MFAACEnc");
}

static obs_properties_t *mfaac_properties(void *unused)
{
	UNUSED_PARAMETER(unused);

	obs_properties_t *props = obs_properties_create();

	obs_properties_add_int(props, "bitrate",
			obs_module_text("Bitrate"), 32, 256, 32);

	return props;
}

static void mfaac_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "bitrate", 128);
}

static HRESULT CreateMediaTypes(ComPtr<IMFMediaType> &i, 
		ComPtr<IMFMediaType> &o, unsigned int sampleRate, 
		unsigned int channels, unsigned int bitsPerSample)
{
	HRESULT hr;
	HRC(MFCreateMediaType(&i));
	HRC(MFCreateMediaType(&o));

	HRC(i->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
	HRC(i->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
	HRC(i->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bitsPerSample));
	HRC(i->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate));
	HRC(i->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels));

	HRC(o->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
	HRC(o->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
	HRC(o->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bitsPerSample));
	HRC(o->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate));
	HRC(o->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels));

	return S_OK;
fail:
	return hr;
}

static void *mfaac_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	int bitrate = (int) obs_data_get_int(settings, "bitrate");
	int bytesPerSec = (bitrate * 1000) / 8;

	HRESULT hr;

	ComPtr<IMFTransform> transform;
	ComPtr<IMFMediaType> inputType, outputType;
	ComPtr<IMFMediaBuffer> outputBuffer;
	ComPtr<IMFSample> outputSample;

	std::unique_ptr<mfaac_encoder> enc(new mfaac_encoder());

	if (!bytesPerSec) {
		blog(LOG_ERROR, "Invalid bitrate");
		return NULL;
	}

	enc->encoder = encoder;
	
	audio_t *audio = obs_encoder_audio(encoder);
	enc->channels = (int)audio_output_get_channels(audio);
	enc->sampleRate = audio_output_get_sample_rate(audio);
	enc->bitsPerSample = 16;

	if (enc->channels > 2) {
		blog(LOG_ERROR, "Invalid channel count");
		goto fail;
	}

	HRC(CoCreateInstance(CLSID_AACMFTEncoder, NULL, CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&transform)));
	HRC(CreateMediaTypes(inputType, outputType, enc->sampleRate,
			enc->channels, enc->bitsPerSample));
	HRC(outputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
			bytesPerSec));
	HRC(transform->SetInputType(0, inputType.Get(), 0));
	HRC(transform->SetOutputType(0, outputType.Get(), 0));

	HRC(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,
			NULL));
	HRC(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,
			NULL));

	enc->frameSizeBytes = 1024 * 2 * enc->channels;
	enc->packetBufferSize = enc->channels * 768;
	if(enc->packetBufferSize < 8192)
		enc->packetBufferSize = 8192;

	enc->packetBuffer.reset(new BYTE[enc->packetBufferSize]);

	HRC(CreateEmptySample(outputSample, enc->packetBufferSize));
	enc->outputSample = outputSample;

	blog(LOG_INFO, "mfaac_aac encoder created");
	blog(LOG_INFO, "mfaac_aac bitrate: %d, channels: %d",
			bitrate, enc->channels);

	enc->transform = transform;

	return enc.release();

fail:
	blog(LOG_WARNING, "mfaac encoder creation failed");
	return 0;
}

static void mfaac_destroy(void *data)
{
	mfaac_encoder *enc = (mfaac_encoder *)data;
	delete data;

	blog(LOG_INFO, "mfaac encoder destroyed");
}

static HRESULT EnsureCapacity(ComPtr<IMFSample> &sample, 
		ComPtr<IMFMediaBuffer> &buffer, DWORD requestedLength)
{
	HRESULT hr;
	DWORD currentLength;

	HRC(buffer->GetMaxLength(&requestedLength));
	if (currentLength < requestedLength) {
		sample->RemoveAllBuffers();
		HRC(MFCreateMemoryBuffer(requestedLength, &buffer));
	}

	return S_OK;
fail:
	return hr;
}

static bool mfaac_encode(void *data, struct encoder_frame *frame,
		struct encoder_packet *packet, bool *received_packet)
{
	mfaac_encoder *enc = (mfaac_encoder *)data;
	HRESULT hr;

	ComPtr<IMFSample> sample;
	ComPtr<IMFMediaBuffer> mediaBuffer;
	BYTE *mediaBufferData;
	DWORD mediaBufferSize;
	MFT_OUTPUT_DATA_BUFFER output = { 0 };
	
	UINT32 samples;
	INT64 samplePts;
	float sampleDur;
	DWORD status;

	HRC(CreateEmptySample(sample, frame->linesize[0]));
	HRC(sample->GetBufferByIndex(0, &mediaBuffer));

	HRC(mediaBuffer->Lock(&mediaBufferData, NULL, NULL));
	memcpy(mediaBufferData, frame->data[0], frame->linesize[0]);
	HRC(mediaBuffer->Unlock());
	HRC(mediaBuffer->SetCurrentLength(frame->linesize[0]));

	samples = frame->linesize[0] / enc->channels /
		(enc->bitsPerSample / 8);
	samplePts = frame->pts / 100;
	sampleDur = (float)enc->sampleRate / enc->channels / samples;
	sample->SetSampleTime(samplePts);
	sample->SetSampleDuration((LONGLONG)(10000 * sampleDur));

	hr = enc->transform->ProcessInput(0, sample.Get(), 0);
	if (hr == MF_E_NOTACCEPTING)
		return false;

	sample.Release();
	mediaBuffer.Release();

	
	sample = enc->outputSample;
	HRC(sample->GetBufferByIndex(0, &mediaBuffer));
	
	output.pSample = sample.Get();

	
	hr = enc->transform->ProcessOutput(0, 1, &output, &status);
	if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
		return true;

	HRC(mediaBuffer->Lock(&mediaBufferData, NULL, &mediaBufferSize));
	memcpy(enc->packetBuffer.get(), mediaBufferData, mediaBufferSize);
	HRC(mediaBuffer->Unlock());
	HRC(mediaBuffer->SetCurrentLength(frame->linesize[0]));

	HRC(sample->GetSampleTime(&samplePts));

	packet->pts = samplePts * 100;
	packet->dts = packet->pts;
	packet->data = enc->packetBuffer.get();
	packet->size = mediaBufferSize;
	packet->type = OBS_ENCODER_AUDIO;
	packet->timebase_num = 1;
	packet->timebase_den = enc->sampleRate;

	return *received_packet = true;
fail:
	return false;
}

static bool mfaac_extra_data(void *data, uint8_t **extra_data, size_t *size)
{
	mfaac_encoder *enc = (mfaac_encoder *)data;

	uint16_t *header = (uint16_t *) enc->header;

#define SWAPU16(x) (x>>8) | (x<<8)
	// LC Profile
	// XXXX X... .... ....
	*header  = 2 << 11;
	// Sample Index (3=48, 4=44.1)
	// .... .XXX X... ....
	*header |= enc->sampleRate == 48000 ? 3 : 4 << 7;
	// Channels
	// .... .... .XXX X...
	*header |= enc->channels << 3;
	*header = SWAPU16(*header);

	// Extensions
	header++;
	*header = 0x2b7 << 5;
	// LC Profile
	*header |= 2;
	*header = SWAPU16(*header);

	enc->header[4] = 0;
#undef SWAPU16

	*extra_data = enc->header;
	*size = 3;
	return true;
}

static void mfaac_audio_info(void *data, struct audio_convert_info *info)
{
	UNUSED_PARAMETER(data);
	info->format = AUDIO_FORMAT_16BIT;
}

static size_t mfaac_frame_size(void *data)
{
	UNUSED_PARAMETER(data);
	// MF Frame size is always 1024
	return 1024;
}

void RegisterMFAACEncoder()
{
	obs_encoder_info info = { 0 };
	info.id = "mf_aac";
	info.type = OBS_ENCODER_AUDIO;
	info.codec = "AAC";
	info.get_name = mfaac_getname;
	info.create = mfaac_create;
	info.destroy = mfaac_destroy;
	info.encode = mfaac_encode;
	info.get_frame_size = mfaac_frame_size;
	info.get_defaults = mfaac_defaults;
	info.get_properties = mfaac_properties;
	info.get_extra_data = mfaac_extra_data;
	info.get_audio_info = mfaac_audio_info;

	obs_register_encoder(&info);
}
