#include <obs-module.h>

#include <memory>

#define WIN32_MEAN_AND_LEAN
#include <Windows.h>
#undef WIN32_MEAN_AND_LEAN

#include <Shlwapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <Mferror.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

using namespace Microsoft::WRL;

struct mfaac_encoder {
	obs_encoder_t *encoder;

    int channels, sample_rate, bits_per_sample;

    ComPtr<IMFTransform> transform;
    ComPtr<IMFSample> output_sample;

	uint64_t total_samples;

	int frame_size_bytes;

	uint8_t *packet_buffer;
	int packet_buffer_size;
    UINT16 header;
    uint8_t header5[5];
};

bool mfaac_extra_data(void *data, uint8_t **extra_data, size_t *size);

static const char *mfaac_getname(void)
{
	return obs_module_text("Media Foundation AAC Encoder");
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

#define HRC(r) \
	if(FAILED(hr = (r))) { \
		blog(LOG_ERROR, #r " failed:  0x%08x", hr); \
		goto fail; \
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

static HRESULT CreateEmptySample(ComPtr<IMFSample> &sample, int length)
{
    HRESULT hr;
    ComPtr<IMFMediaBuffer> mediaBuffer;

    HRC(MFCreateSample(&sample));
    HRC(MFCreateMemoryBuffer(length, &mediaBuffer));
    HRC(sample->AddBuffer(mediaBuffer.Get()));
    return S_OK;
fail:
    return hr;
}

static void *mfaac_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	bool hasFdkHandle = false;
    int bitrate = (int) obs_data_get_int(settings, "bitrate");
    int bytes_per_sec = (bitrate * 1000) / 8;
    
	int mode = 0;
    HRESULT hr;

    ComPtr<IMFTransform> transform;
    ComPtr<IMFMediaType> inputType, outputType;
    ComPtr<IMFMediaBuffer> outputBuffer;
    ComPtr<IMFSample> outputSample;

    std::unique_ptr<mfaac_encoder> enc(new mfaac_encoder());

    if (!bytes_per_sec) {
		blog(LOG_ERROR, "Invalid bitrate");
		return NULL;
	}

    enc->encoder = encoder;
	
    audio_t *audio = obs_encoder_audio(encoder);
    enc->channels = (int)audio_output_get_channels(audio);
	enc->sample_rate = audio_output_get_sample_rate(audio);
    enc->bits_per_sample = 16;

	if (enc->channels > 2) {
		blog(LOG_ERROR, "Invalid channel count");
		goto fail;
	}

    HRC(CoCreateInstance(CLSID_AACMFTEncoder, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&transform)));
    HRC(CreateMediaTypes(inputType, outputType, enc->sample_rate, enc->channels, enc->bits_per_sample));
    HRC(outputType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bytes_per_sec));
    HRC(transform->SetInputType(0, inputType.Get(), 0));
    HRC(transform->SetOutputType(0, outputType.Get(), 0));

    hasFdkHandle = true;

    HRC(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL));
    HRC(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL));

	enc->frame_size_bytes = 1024 * 2 * enc->channels;
	enc->packet_buffer_size = enc->channels * 768;
	if(enc->packet_buffer_size < 8192)
		enc->packet_buffer_size = 8192;

	enc->packet_buffer = (uint8_t *)bmalloc(enc->packet_buffer_size);

    HRC(CreateEmptySample(outputSample, enc->packet_buffer_size));
    enc->output_sample = outputSample;

	blog(LOG_INFO, "mfaac_aac encoder created");
	blog(LOG_INFO, "mfaac_aac bitrate: %d, channels: %d",
			bitrate, enc->channels);

    UINT8 *data;
    size_t size;
    
    enc->transform = transform;
    
    mfaac_extra_data(enc.get(), &data, &size);

    return enc.release();

fail:

	blog(LOG_WARNING, "mfaac encoder creation failed");

	return 0;
}

static void mfaac_destroy(void *data)
{
	mfaac_encoder *enc = (mfaac_encoder *)data;

	bfree(enc->packet_buffer);

    delete data;

	blog(LOG_INFO, "mfaac encoder destroyed");
}

static bool mfaac_encode(void *data, struct encoder_frame *frame,
                          struct encoder_packet *packet, bool *received_packet)
{
	mfaac_encoder *enc = (mfaac_encoder *)data;
    HRESULT hr;

    ComPtr<IMFSample> sample;
    ComPtr<IMFMediaBuffer> mediaBuffer;

    HRC(CreateEmptySample(sample, frame->linesize[0]));
    HRC(sample->GetBufferByIndex(0, &mediaBuffer));
    
    BYTE *mediaBufferData;
    HRC(mediaBuffer->Lock(&mediaBufferData, NULL, NULL));
    memcpy(mediaBufferData, frame->data[0], frame->linesize[0]);
    HRC(mediaBuffer->Unlock());
    HRC(mediaBuffer->SetCurrentLength(frame->linesize[0]));

    UINT32 samples = frame->linesize[0] / enc->channels / (enc->bits_per_sample / 8); 
    INT64 samplePts = frame->pts / 100;
    float sampleDur = (float)enc->sample_rate / enc->channels / samples;
    sample->SetSampleTime(samplePts);
    sample->SetSampleDuration(10000 * sampleDur);
    DWORD inputStatus;
    HRC(enc->transform->GetInputStatus(0, &inputStatus));
    
    hr = enc->transform->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING)
        return false;

    sample.Reset();
    mediaBuffer.Reset();

    sample = enc->output_sample;
    HRC(sample->GetBufferByIndex(0, &mediaBuffer));

    MFT_OUTPUT_DATA_BUFFER output = { 0 };
    output.pSample = sample.Get();

    DWORD status;
    hr = enc->transform->ProcessOutput(0, 1, &output, &status);
    if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
        return true;
    
    DWORD mediaBufferSize;
    HRC(mediaBuffer->Lock(&mediaBufferData, NULL, &mediaBufferSize));
    memcpy(enc->packet_buffer, mediaBufferData, mediaBufferSize);
    HRC(mediaBuffer->Unlock());
    HRC(mediaBuffer->SetCurrentLength(frame->linesize[0]));

    HRC(sample->GetSampleTime(&samplePts));

    packet->pts = samplePts * 100;
    packet->dts = packet->pts;
	packet->data = enc->packet_buffer;
	packet->size = mediaBufferSize;
	packet->type = OBS_ENCODER_AUDIO;
	packet->timebase_num = 1;
	packet->timebase_den = enc->sample_rate;

    return *received_packet = true;
fail:
    return false;
}

static bool mfaac_extra_data(void *data, uint8_t **extra_data, size_t *size)
{
	mfaac_encoder *enc = (mfaac_encoder *)data;

    uint16_t *header = (uint16_t *) enc->header5;

#define SWAPU16(x) (x>>8) | (x<<8)
    // LC Profile
    // XXXX X... .... ....
    *header  = 2 << 11;
    // Sample Index (3=48, 4=44.1)
    // .... .XXX X... ....
    *header |= enc->sample_rate == 48000 ? 3 : 4 << 7;
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

    enc->header5[4] = 0;
#undef SWAPU16

    *extra_data = enc->header5;
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

bool obs_module_load(void)
{
    RegisterMFAACEncoder();
	return true;
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-mfaac", "en-US")
