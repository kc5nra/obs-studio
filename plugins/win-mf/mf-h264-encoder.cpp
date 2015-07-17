#include <obs-module.h>

#include <memory>

#include <codecapi.h>

#include <fstream>

extern "C" {
void RegisterMFH264Encoder();
}

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
#include <functional>

#include <util/windows/ComPtr.hpp>

#define HRC(r) \
	if(FAILED(hr = (r))) { \
		blog(LOG_ERROR, #r " failed:  0x%08x", hr); \
		goto fail; \
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

DEFINE_GUID(CLSID_MF_H264QSEnc,
		0x08b2f572, 0x51bf, 0x4e93, 0x8b, 0x15, 0x33, 0x86, 0x45, 0x46, 
		0xdc, 0x9a);

struct mf_h264_encoder
{
	obs_encoder_t *encoder;
	ComPtr<IMFTransform> transform;

	video_format format;
	UINT32 width;
	UINT32 height;
	UINT32 fps_num;
	UINT32 fps_den;
	UINT32 bitrate;
	std::ofstream outputBuffer;
	BYTE *output;
	BYTE *extra_data = NULL;
	size_t extra_data_size = 0;
	DWORD outputSize;
	const char* profile;
};

static const char *mf_h264_getname(void)
{
	return obs_module_text("MFH264Enc");
}

#define TEXT_BITRATE    obs_module_text("Bitrate")
#define TEXT_CUSTOM_BUF obs_module_text("CustomBufsize")
#define TEXT_BUF_SIZE   obs_module_text("BufferSize")
#define TEXT_USE_CBR    obs_module_text("UseCBR")
#define TEXT_CRF        obs_module_text("CRF")
#define TEXT_KEYINT_SEC obs_module_text("KeyframeIntervalSec")
#define TEXT_PRESET     obs_module_text("CPUPreset")
#define TEXT_PROFILE    obs_module_text("Profile")
#define TEXT_NONE       obs_module_text("None")

static bool use_bufsize_modified(obs_properties_t *ppts, obs_property_t *p,
	obs_data_t *settings)
{
	bool use_bufsize = obs_data_get_bool(settings, "use_bufsize");
	p = obs_properties_get(ppts, "buffer_size");
	obs_property_set_visible(p, use_bufsize);
	return true;
}

static bool use_cbr_modified(obs_properties_t *ppts, obs_property_t *p,
	obs_data_t *settings)
{
	bool cbr = obs_data_get_bool(settings, "cbr");
	p = obs_properties_get(ppts, "crf");
	obs_property_set_visible(p, !cbr);
	return true;
}

static obs_properties_t *mf_h264_properties(void *unused)
{

	UNUSED_PARAMETER(unused);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *list;
	obs_property_t *p;

	obs_properties_add_int(props, "bitrate", TEXT_BITRATE, 50, 10000000, 1);

	p = obs_properties_add_bool(props, "use_bufsize", TEXT_CUSTOM_BUF);
	obs_property_set_modified_callback(p, use_bufsize_modified);
	obs_properties_add_int(props, "buffer_size", TEXT_BUF_SIZE, 0,
		10000000, 1);

	obs_properties_add_int(props, "keyint_sec", TEXT_KEYINT_SEC, 0, 20, 1);
	p = obs_properties_add_bool(props, "cbr", TEXT_USE_CBR);
	obs_properties_add_int(props, "crf", TEXT_CRF, 0, 51, 1);

	obs_property_set_modified_callback(p, use_cbr_modified);

	list = obs_properties_add_list(props, "profile", TEXT_PROFILE,
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(list, TEXT_NONE, "");
	obs_property_list_add_string(list, "baseline", "baseline");
	obs_property_list_add_string(list, "main", "main");
	obs_property_list_add_string(list, "high", "high");

	return props;
}


static void mf_h264_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "bitrate", 2500);
	obs_data_set_default_bool(settings, "use_bufsize", false);
	obs_data_set_default_int(settings, "buffer_size", 2500);
	obs_data_set_default_int(settings, "keyint_sec", 0);
	obs_data_set_default_int(settings, "crf", 23);
	obs_data_set_default_bool(settings, "cbr", true);
	obs_data_set_default_string(settings, "profile", "");
}

static bool mf_h264_update(void *data, obs_data_t *settings)
{
	return true;
}

static bool mf_h264_sei_data(void *data, uint8_t **sei, size_t *size)
{
	return false;
}

static GUID MapInputFormat(video_format format)
{
	switch (format)
	{
	case VIDEO_FORMAT_NV12: return MFVideoFormat_NV12;
	case VIDEO_FORMAT_I420: return MFVideoFormat_I420;
	case VIDEO_FORMAT_YUY2: return MFVideoFormat_YUY2;
	default: return MFVideoFormat_NV12;
	}
}

static eAVEncH264VProfile MapProfile(const char *profile)
{
	eAVEncH264VProfile p = eAVEncH264VProfile_Base;
	if (strcmp(profile, "main") == 0)
		p = eAVEncH264VProfile_Main;
	else if (strcmp(profile, "high") == 0)
		p = eAVEncH264VProfile_High;
	return p;
}

static HRESULT CreateMediaTypes(ComPtr<IMFMediaType> &i,
		ComPtr<IMFMediaType> &o, mf_h264_encoder *enc)
{
	HRESULT hr;
	HRC(MFCreateMediaType(&i));
	HRC(MFCreateMediaType(&o));

	HRC(i->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	HRC(i->SetGUID(MF_MT_SUBTYPE, MapInputFormat(enc->format)));
	HRC(MFSetAttributeSize(i, MF_MT_FRAME_SIZE, enc->width, enc->height));
	HRC(MFSetAttributeRatio(i, MF_MT_FRAME_RATE, enc->fps_num, enc->fps_den));

	HRC(o->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	HRC(o->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
	HRC(MFSetAttributeSize(o, MF_MT_FRAME_SIZE, enc->width, enc->height));
	HRC(MFSetAttributeRatio(o, MF_MT_FRAME_RATE, enc->fps_num, enc->fps_den));
	HRC(o->SetUINT32(MF_MT_AVG_BITRATE, enc->bitrate * 1000));
	HRC(o->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlaceMode::MFVideoInterlace_Progressive));
	HRC(MFSetAttributeRatio(o, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
	HRC(o->SetUINT32(MF_MT_MPEG2_LEVEL, -1));
	HRC(o->SetUINT32(MF_MT_MPEG2_PROFILE, MapProfile(enc->profile)));

	return S_OK;
fail:
	return hr;
}

static void mf_h264_video_info(void *data, struct video_scale_info *info);

static void update_params(mf_h264_encoder *enc, obs_data_t *settings)
{
	video_t *video = obs_encoder_video(enc->encoder);
	const struct video_output_info *voi = video_output_get_info(video);
	struct video_scale_info info;

	const char *profile = obs_data_get_string(settings, "profile");

	info.format = voi->format;
	info.colorspace = voi->colorspace;
	info.range = voi->range;

	mf_h264_video_info(enc, &info);

	enc->format = info.format;
	enc->width = obs_encoder_get_width(enc->encoder);
	enc->height = obs_encoder_get_height(enc->encoder);
	enc->fps_num = voi->fps_num;
	enc->fps_den = voi->fps_den;
	enc->bitrate = (UINT32)obs_data_get_int(settings, "bitrate");
	enc->profile = obs_data_get_string(settings, "profile");
	
	
}

static inline bool valid_format(enum video_format format)
{
	return format == VIDEO_FORMAT_NV12;
}

static void mf_h264_video_info(void *data, struct video_scale_info *info)
{
	mf_h264_encoder *enc = (mf_h264_encoder *)data;
	enum video_format pref_format;

	pref_format = obs_encoder_get_preferred_video_format(enc->encoder);

	if (!valid_format(pref_format)) {
		pref_format = valid_format(info->format) ?
			info->format : VIDEO_FORMAT_NV12;
	}

	info->format = pref_format;
}

static void *mf_h264_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	HRESULT hr;

	ComPtr<IMFTransform> transform;
	ComPtr<IMFMediaType> inputType, outputType;

	std::unique_ptr<mf_h264_encoder> enc(new mf_h264_encoder());

	enc->encoder = encoder;

	update_params(enc.get(), settings);

	HRC(CoCreateInstance(CLSID_CMSH264EncoderMFT, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&transform)));

	CreateMediaTypes(inputType, outputType, enc.get());
	HRC(transform->SetOutputType(0, outputType, 0));
	HRC(transform->SetInputType(0, inputType, 0));
	
	HRC(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,
			NULL));
	HRC(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,
			NULL));

	enc->transform = transform;

	enc->outputBuffer.open("out.h264", std::ios::binary | std::ios::out);
	enc->output = NULL;
	enc->outputSize = 0;
	return enc.release();
fail:
	return NULL;
}

static void mf_h264_destroy(void *data)
{
	mf_h264_encoder *enc = (mf_h264_encoder *)data;
	enc->outputBuffer.close();

	delete enc;
	blog(LOG_INFO, "mfh264 encoder destroyed");
}

static inline size_t memcpy_and_inc(BYTE **dst, BYTE *src, size_t size)
{
	memcpy(*dst, src, size);
	*dst += size;
}

bool ProcessFormat(std::function<void(size_t height, int plane)> func,
		enum video_format format, uint32_t height)
{
	HRESULT hr;
	int plane = 0;

	switch (format) {
	case VIDEO_FORMAT_NONE:
		return false;

	case VIDEO_FORMAT_I420:
		func(height, plane++);
		func(height / 2, plane++);
		func(height / 2, plane);
		break;

	case VIDEO_FORMAT_NV12:
		func(height, plane++);
		func(height / 2, plane);
		break;

	case VIDEO_FORMAT_YVYU:
	case VIDEO_FORMAT_YUY2:
	case VIDEO_FORMAT_UYVY:
	case VIDEO_FORMAT_RGBA:
	case VIDEO_FORMAT_BGRA:
	case VIDEO_FORMAT_BGRX:
		func(height, plane);
		break;

	case VIDEO_FORMAT_I444:
		func(height, plane++);
		func(height, plane++);
		func(height, plane);
		break;
	}

	return true;
}

static bool mf_h264_encode(void *data, struct encoder_frame *frame,
		struct encoder_packet *packet, bool *received_packet)
{
	mf_h264_encoder *enc = (mf_h264_encoder *)data;
	HRESULT hr;
	UINT32 frameSize;
	ComPtr<IMFSample> sample;
	ComPtr<IMFMediaBuffer> mediaBuffer;
	ComPtr<IMFAttributes> mediaBufferAttributes;
	ComPtr<IMFMediaBuffer> contiguousBuffer;
	BYTE *mediaBufferData;
	INT64 samplePts;
	UINT64 sampleDur;
	MFT_OUTPUT_STREAM_INFO outputInfo = { 0 };
	DWORD outputFlags, outputStatus, outputSize;
	MFT_OUTPUT_DATA_BUFFER output = { 0 };
	DWORD sampleFlags;
	DWORD bufferCount;

	frameSize = 0;
	ProcessFormat([&](size_t height, int plane) {
		frameSize += frame->linesize[plane] * height;
	}, enc->format, enc->height);
	
	HRC(CreateEmptySample(sample, frameSize));
	HRC(sample->GetBufferByIndex(0, &mediaBuffer));

	HRC(mediaBuffer->Lock(&mediaBufferData, NULL, NULL));

	ProcessFormat([&](size_t height, int plane) {
		size_t l = frame->linesize[plane] * height;
		memcpy(mediaBufferData, frame->data[plane], l);
		mediaBufferData += l;
	}, enc->format, enc->height);
	HRC(mediaBuffer->Unlock());

	HRC(mediaBuffer->SetCurrentLength(frameSize));

	MFFrameRateToAverageTimePerFrame(enc->fps_num, enc->fps_den, &sampleDur);
	
	HRC(sample->SetSampleTime(frame->pts));
	HRC(sample->SetSampleDuration(sampleDur));

	hr = enc->transform->ProcessInput(0, sample, 0);
	if (hr == MF_E_NOTACCEPTING)
		return false;

	sample.Release();
	mediaBuffer.Release();

	HRC(enc->transform->GetOutputStatus(&outputFlags));
	if (outputFlags != MFT_OUTPUT_STATUS_SAMPLE_READY)
		return true;

	HRC(enc->transform->GetOutputStreamInfo(0, &outputInfo));
	CreateEmptySample(sample, outputInfo.cbSize);
	HRC(sample->GetBufferByIndex(0, &mediaBuffer));
	HRC(sample->GetBufferCount(&bufferCount));
	
	output.pSample = sample;
	hr = enc->transform->ProcessOutput(0, 1, &output, &outputStatus);
	if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
		return true;
	
	bool isKeyFrame = !!MFGetAttributeUINT32(sample, MFSampleExtension_CleanPoint, FALSE);
	
	DWORD len;
	mediaBuffer->GetCurrentLength(&len);

	HRC(mediaBuffer->Lock(&mediaBufferData, NULL, &outputSize));
	
	if (!enc->output || enc->outputSize < outputSize + enc->extra_data_size) {
		if (enc->output)
			bfree(enc->output);
		enc->output = (BYTE *) bzalloc(outputSize + enc->extra_data_size);
		enc->outputSize = outputSize + enc->extra_data_size;
	}
	BYTE *out = enc->output;
	if (isKeyFrame) {
		memcpy(out, enc->extra_data, enc->extra_data_size);
		out += enc->extra_data_size;
	}
	else {
		mediaBufferData += 1;
		outputSize -= 1;
	}
	enc->outputBuffer.write((const char *)mediaBufferData, outputSize);
	memcpy(out, mediaBufferData, outputSize);
	HRC(mediaBuffer->Unlock());

	HRC(sample->GetSampleTime(&samplePts));

	packet->pts = samplePts;
	packet->dts = packet->pts;
	packet->data = enc->output;
	packet->size = isKeyFrame ? enc->outputSize : enc->outputSize + enc->extra_data_size;
	packet->type = OBS_ENCODER_VIDEO;
	packet->keyframe = isKeyFrame;
	
	return *received_packet = true;
fail:
	return false;
}

static bool mf_h264_extra_data(void *data, uint8_t **extra_data, size_t *size)
{
	mf_h264_encoder *enc = (mf_h264_encoder *)data;

	HRESULT hr;
	MPEG2VIDEOINFO *mediaInfo;
	ComPtr<IMFMediaType> inputType;
	UINT32 headerSize;
	BYTE *header;
	HRC(enc->transform->GetOutputCurrentType(0, &inputType));

	HRC(inputType->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &headerSize));
	header = new BYTE[headerSize];
	HRC(inputType->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, header, headerSize, NULL));

	enc->extra_data = header;
	enc->extra_data_size = headerSize;
	
	//*extra_data = enc->extra_data;
	//*size = enc->extra_data_size;
	return true;
fail:
	return false;
}

void RegisterMFH264Encoder()
{
	obs_encoder_info info = { 0 };
	info.id = "mf_h264";
	info.type = OBS_ENCODER_VIDEO;
	info.codec = "h264";
	info.get_name = mf_h264_getname;
	info.create = mf_h264_create;
	info.destroy = mf_h264_destroy;
	info.encode = mf_h264_encode;
	info.update = mf_h264_update;
	info.get_properties = mf_h264_properties;
	info.get_defaults = mf_h264_defaults;
	info.get_extra_data = mf_h264_extra_data;
	info.get_sei_data = mf_h264_sei_data;
	info.get_video_info = mf_h264_video_info;

	obs_register_encoder(&info);
}