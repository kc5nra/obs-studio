#include <obs-module.h>
#include <util/profiler.hpp>

#include <memory>
#include <chrono>

#include "mf-h264-encoder.hpp"
#include "mf-encoder-descriptor.hpp"
#include <VersionHelpers.h>

using namespace MF;

struct MFH264_Encoder {
	obs_encoder_t *encoder;
	std::shared_ptr<EncoderDescriptor> descriptor;
	std::unique_ptr<H264Encoder> h264Encoder;
	uint32_t width;
	uint32_t height;
	uint32_t framerateNum;
	uint32_t framerateDen;
	uint32_t keyint;
	bool advanced;
	uint32_t bitrate;
	uint32_t maxBitrate;
	bool useMaxBitrate;
	uint32_t bufferSize;
	bool useBufferSize;
	H264Profile profile;
	H264RateControl rateControl;
	H264QP qp;
	uint32_t minQp;
	uint32_t maxQp;
	bool lowLatency;
	uint32_t bFrames;
};

#define TEXT_ENCODER         obs_module_text("Encoder")
#define TEXT_ADVANCED        obs_module_text("Advanced")
#define TEXT_LOW_LAT         obs_module_text("LowLatency")
#define TEXT_B_FRAMES        obs_module_text("BFrames")
#define TEXT_BITRATE         obs_module_text("Bitrate")
#define TEXT_CUSTOM_BUF      obs_module_text("CustomBufsize")
#define TEXT_BUF_SIZE        obs_module_text("BufferSize")
#define TEXT_USE_MAX_BITRATE obs_module_text("CustomMaxBitrate")
#define TEXT_MAX_BITRATE     obs_module_text("MaxBitrate")
#define TEXT_KEYINT_SEC      obs_module_text("KeyframeIntervalSec")
#define TEXT_RATE_CONTROL    obs_module_text("RateControl")
#define TEXT_MIN_QP          obs_module_text("MinQP")
#define TEXT_MAX_QP          obs_module_text("MaxQP")
#define TEXT_QPI             obs_module_text("QPI")
#define TEXT_QPP             obs_module_text("QPP")
#define TEXT_QPB             obs_module_text("QPB")
#define TEXT_PROFILE         obs_module_text("Profile")
#define TEXT_CBR             obs_module_text("CBR")
#define TEXT_VBR             obs_module_text("VBR")
#define TEXT_CQP             obs_module_text("CQP")

#define MFP(x) "mf_h264_" ## x
#define MFP_ENCODER_GUID    MFP("encoder_guid")
#define MFP_USE_ADVANCED    MFP("use_advanced")
#define MFP_USE_LOWLAT      MFP("use_low_latency")
#define MFP_B_FRAMES        MFP("b_frames")
#define MFP_BITRATE         MFP("bitrate")
#define MFP_USE_BUF_SIZE    MFP("use_buf_size")
#define MFP_BUF_SIZE        MFP("buf_size")
#define MFP_USE_MAX_BITRATE MFP("use_max_bitrate")
#define MFP_MAX_BITRATE     MFP("use_max_bitrate")
#define MFP_KEY_INT         MFP("key_int")
#define MFP_RATE_CONTROL    MFP("rate_control")
#define MFP_MIN_QP          MFP("min_qp")
#define MFP_MAX_QP          MFP("max_qp")
#define MFP_QP_I            MFP("qp_i")
#define MFP_QP_P            MFP("qp_p")
#define MFP_QP_B            MFP("qp_b")
#define MFP_PROFILE         MFP("profile")

static const char *MFH264_GetName()
{
	return obs_module_text("MFH264Enc");
}

static void set_visible(obs_properties_t *ppts, const char *name, bool visible)
{
	obs_property_t *p = obs_properties_get(ppts, name);
	obs_property_set_visible(p, visible);
}

static bool use_bufsize_modified(obs_properties_t *ppts, obs_property_t *p,
	obs_data_t *settings)
{
	UNUSED_PARAMETER(p);

	bool use_bufsize = obs_data_get_bool(settings, MFP_USE_BUF_SIZE);
	set_visible(ppts, MFP_BUF_SIZE, use_bufsize);

	return true;
}

static bool use_max_bitrate_modified(obs_properties_t *ppts, obs_property_t *p,
	obs_data_t *settings)
{
	UNUSED_PARAMETER(p);

	bool advanced = obs_data_get_bool(settings, MFP_USE_ADVANCED);
	bool use_max_bitrate = obs_data_get_bool(settings, MFP_USE_MAX_BITRATE);
	set_visible(ppts, MFP_MAX_BITRATE, advanced && use_max_bitrate);

	return true;
}

static bool use_advanced_modified(obs_properties_t *ppts, obs_property_t *p,
	obs_data_t *settings)
{
	bool advanced = obs_data_get_bool(settings, MFP_USE_ADVANCED);

	set_visible(ppts, MFP_MIN_QP,       advanced);
	set_visible(ppts, MFP_MAX_QP,       advanced);
	set_visible(ppts, MFP_USE_LOWLAT,  advanced);
	set_visible(ppts, MFP_B_FRAMES,     advanced);

	H264RateControl rateControl = (H264RateControl)obs_data_get_int(
		settings, MFP_RATE_CONTROL);

	if (rateControl == H264RateControlCBR ||
	    rateControl == H264RateControlVBR) {
		set_visible(ppts, MFP_USE_MAX_BITRATE, advanced);
		use_max_bitrate_modified(ppts, NULL, settings);
	}

	return true;
}

static bool rate_control_modified(obs_properties_t *ppts, obs_property_t *p,
	obs_data_t *settings)
{
	H264RateControl rateControl = (H264RateControl)obs_data_get_int(
		settings, MFP_RATE_CONTROL);

	bool advanced = obs_data_get_bool(settings, MFP_USE_ADVANCED);

	set_visible(ppts, MFP_BITRATE,         false);
	set_visible(ppts, MFP_USE_BUF_SIZE,    false);
	set_visible(ppts, MFP_BUF_SIZE,        false);
	set_visible(ppts, MFP_USE_MAX_BITRATE, false);
	set_visible(ppts, MFP_MAX_BITRATE,     false);
	set_visible(ppts, MFP_QP_I,            false);
	set_visible(ppts, MFP_QP_P,            false);
	set_visible(ppts, MFP_QP_B,            false);

	switch (rateControl) {
	case H264RateControlCBR:
		use_bufsize_modified(ppts,     NULL, settings);
		use_max_bitrate_modified(ppts, NULL, settings);

		set_visible(ppts, MFP_BITRATE,         true);
		set_visible(ppts, MFP_USE_BUF_SIZE,    true);
		set_visible(ppts, MFP_USE_MAX_BITRATE, advanced);

		break;
	case H264RateControlVBR:
		use_bufsize_modified(ppts,     NULL, settings);
		use_max_bitrate_modified(ppts, NULL, settings);

		set_visible(ppts, MFP_BITRATE,         true);
		set_visible(ppts, MFP_USE_BUF_SIZE,    true);
		set_visible(ppts, MFP_USE_MAX_BITRATE, advanced);

		break;
	case H264RateControlCQP:
		set_visible(ppts, MFP_QP_I,            true);
		set_visible(ppts, MFP_QP_P,            true);
		set_visible(ppts, MFP_QP_B,            true);

		break;
	default: break;
	}

	return true;
}

static obs_properties_t *MFH264_GetProperties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *p = obs_properties_add_list(props, MFP_ENCODER_GUID,
			TEXT_ENCODER, OBS_COMBO_TYPE_LIST,
			OBS_COMBO_FORMAT_STRING);
	auto encoders = MF::EncoderDescriptor::Enumerate();
	for (auto e : encoders) {
		obs_property_list_add_string(p,
				obs_module_text(e->Name().c_str()),
				e->GuidString().c_str());
	}

	obs_property_t *list = obs_properties_add_list(props, MFP_PROFILE,
		TEXT_PROFILE, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(list, "baseline", H264ProfileBaseline);
	obs_property_list_add_int(list, "main",     H264ProfileMain);
	obs_property_list_add_int(list, "high",     H264ProfileHigh);

	obs_properties_add_int(props, MFP_KEY_INT, TEXT_KEYINT_SEC, 0, 20, 1);

	list = obs_properties_add_list(props, MFP_RATE_CONTROL,
		TEXT_RATE_CONTROL, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);

	obs_property_list_add_int(list, TEXT_CBR, H264RateControlCBR);
	obs_property_list_add_int(list, TEXT_VBR, H264RateControlVBR);
	obs_property_list_add_int(list, TEXT_CQP, H264RateControlCQP);

	obs_property_set_modified_callback(list, rate_control_modified);

	obs_properties_add_int(props, MFP_BITRATE, TEXT_BITRATE, 50, 10000000,
			1);

	p = obs_properties_add_bool(props, MFP_USE_BUF_SIZE, TEXT_CUSTOM_BUF);
	obs_property_set_modified_callback(p, use_bufsize_modified);
	obs_properties_add_int(props, MFP_BUF_SIZE, TEXT_BUF_SIZE, 0,
			10000000, 1);

	obs_properties_add_int(props, MFP_QP_I, TEXT_QPI, 0, 51, 1);
	obs_properties_add_int(props, MFP_QP_P, TEXT_QPP, 0, 51, 1);
	obs_properties_add_int(props, MFP_QP_B, TEXT_QPB, 0, 51, 1);

	p = obs_properties_add_bool(props, MFP_USE_ADVANCED, TEXT_ADVANCED);
	obs_property_set_modified_callback(p, use_advanced_modified);

	p = obs_properties_add_bool(props, MFP_USE_MAX_BITRATE,
		TEXT_USE_MAX_BITRATE);
	obs_property_set_modified_callback(p, use_max_bitrate_modified);
	obs_properties_add_int(props, MFP_MAX_BITRATE, TEXT_MAX_BITRATE, 50,
		10000000, 1);

	obs_properties_add_bool(props, MFP_USE_LOWLAT, TEXT_LOW_LAT);
	obs_properties_add_int(props,  MFP_B_FRAMES,   TEXT_B_FRAMES, 0, 16, 1);
	obs_properties_add_int(props,  MFP_MIN_QP,     TEXT_MIN_QP,   1, 51, 1);
	obs_properties_add_int(props,  MFP_MAX_QP,     TEXT_MAX_QP,   1, 51, 1);
		return props;
}

#define MF_H264_DEFAULT_GUID "{6CA50344-051A-4DED-9779-A43305165E35}"

static void MFH264_GetDefaults(obs_data_t *settings)
{
#define PROP_DEF(x, y, z) obs_data_set_default_ ## x(settings, y, z)
	PROP_DEF(string, MFP_ENCODER_GUID, MF_H264_DEFAULT_GUID);
	PROP_DEF(int,    MFP_BITRATE,         2500);
	PROP_DEF(bool,   MFP_USE_LOWLAT,      true);
	PROP_DEF(int,    MFP_B_FRAMES,        2);
	PROP_DEF(bool,   MFP_USE_BUF_SIZE,    false);
	PROP_DEF(int,    MFP_BUF_SIZE,        2500);
	PROP_DEF(bool,   MFP_USE_MAX_BITRATE, false);
	PROP_DEF(int,    MFP_MAX_BITRATE,     2500);
	PROP_DEF(int,    MFP_KEY_INT,         2);
	PROP_DEF(int,    MFP_RATE_CONTROL,    H264RateControlCBR);
	PROP_DEF(int,    MFP_PROFILE,         H264ProfileMain);
	PROP_DEF(int,    MFP_MIN_QP,          1);
	PROP_DEF(int,    MFP_MAX_QP,          51);
	PROP_DEF(int,    MFP_QP_I,            26);
	PROP_DEF(int,    MFP_QP_B,            26);
	PROP_DEF(int,    MFP_QP_P,            26);
	PROP_DEF(bool,   MFP_USE_ADVANCED,    false);
#undef DEF
}

static void UpdateParams(MFH264_Encoder *enc, obs_data_t *settings)
{
	video_t *video = obs_encoder_video(enc->encoder);
	const struct video_output_info *voi = video_output_get_info(video);

	enc->width = (uint32_t)obs_encoder_get_width(enc->encoder);
	enc->height = (uint32_t)obs_encoder_get_height(enc->encoder);
	enc->framerateNum = voi->fps_num;
	enc->framerateDen = voi->fps_den;

	std::shared_ptr<EncoderDescriptor> ed;

	std::string encoderGuid = obs_data_get_string(settings,
		MFP_ENCODER_GUID);
	auto encoders = MF::EncoderDescriptor::Enumerate();
	for (auto e : encoders) {
		if (!ed) {
			ed = e;
		}
		if (e->GuidString() == encoderGuid) {
			ed = e;
		}
	}
	enc->descriptor = ed;

#define PROP_GET(x, y, z) (z)obs_data_get_ ## x(settings, y)
	enc->profile       = PROP_GET(int,  MFP_PROFILE,      H264Profile);
	enc->rateControl   = PROP_GET(int,  MFP_RATE_CONTROL, H264RateControl);
	enc->keyint        = PROP_GET(int,  MFP_KEY_INT,      uint32_t);
	enc->bitrate       = PROP_GET(int,  MFP_BITRATE,      uint32_t);
	enc->useBufferSize = PROP_GET(bool, MFP_USE_BUF_SIZE, bool);
	enc->bufferSize    = PROP_GET(int,  MFP_BUF_SIZE,     uint32_t);
	enc->useMaxBitrate = PROP_GET(bool, MFP_USE_MAX_BITRATE, uint32_t);
	enc->maxBitrate    = PROP_GET(int,  MFP_MAX_BITRATE,  uint32_t);
	enc->minQp         = PROP_GET(int,  MFP_MIN_QP,       uint16_t);
	enc->maxQp         = PROP_GET(int,  MFP_MAX_QP,       uint16_t);
	enc->qp.defaultQp  = PROP_GET(int,  MFP_QP_I,         uint16_t);
	enc->qp.i          = PROP_GET(int,  MFP_QP_I,         uint16_t);
	enc->qp.p          = PROP_GET(int,  MFP_QP_P,         uint16_t);
	enc->qp.b          = PROP_GET(int,  MFP_QP_B,         uint16_t);
	enc->lowLatency    = PROP_GET(bool, MFP_USE_LOWLAT,   bool);
	enc->bFrames       = PROP_GET(int,  MFP_B_FRAMES,     uint32_t);
	enc->advanced      = PROP_GET(bool, MFP_USE_ADVANCED, bool);
#undef PROP_GET
}

#undef MFP

static bool ApplyCBR(MFH264_Encoder *enc)
{
	enc->h264Encoder->SetBitrate(enc->bitrate);

	if (enc->useMaxBitrate)
		enc->h264Encoder->SetMaxBitrate(enc->maxBitrate);
	else
		enc->h264Encoder->SetMaxBitrate(enc->bitrate);

	if (enc->useBufferSize)
		enc->h264Encoder->SetBufferSize(enc->bufferSize);

	return true;
}

static bool ApplyCVBR(MFH264_Encoder *enc)
{
	enc->h264Encoder->SetBitrate(enc->bitrate);

	if (enc->advanced && enc->useMaxBitrate)
		enc->h264Encoder->SetMaxBitrate(enc->maxBitrate);
	else
		enc->h264Encoder->SetMaxBitrate(enc->bitrate);

	if (enc->useBufferSize)
		enc->h264Encoder->SetBufferSize(enc->bufferSize);

	return true;
}

static bool ApplyVBR(MFH264_Encoder *enc)
{
	enc->h264Encoder->SetBitrate(enc->bitrate);

	if (enc->useBufferSize)
		enc->h264Encoder->SetBufferSize(enc->bufferSize);

	return true;
}

static bool ApplyCQP(MFH264_Encoder *enc)
{
	enc->h264Encoder->SetQP(enc->qp);

	return true;
}

static void *MFH264_Create(obs_data_t *settings, obs_encoder_t *encoder)
{
	ProfileScope("MFH264_Create");

	std::unique_ptr<MFH264_Encoder> enc(new MFH264_Encoder());
	enc->encoder = encoder;

	UpdateParams(enc.get(), settings);

	enc->h264Encoder.reset(new H264Encoder(encoder,
			enc->descriptor,
			enc->width,
			enc->height,
			enc->framerateNum,
			enc->framerateDen,
			enc->profile,
			enc->bitrate));

	auto applySettings = [&]() {
		enc.get()->h264Encoder->SetRateControl(enc->rateControl);
		enc.get()->h264Encoder->SetKeyframeInterval(enc->keyint);

		enc.get()->h264Encoder->SetEntropyEncoding(
				H264EntopyEncodingCAVAC);

		if (enc->advanced) {
			enc.get()->h264Encoder->SetLowLatency(enc->lowLatency);
			enc.get()->h264Encoder->SetBFrameCount(enc->bFrames);

			enc.get()->h264Encoder->SetMinQP(enc->minQp);
			enc.get()->h264Encoder->SetMaxQP(enc->maxQp);
		}

		if (enc->rateControl == H264RateControlVBR &&
		    enc->advanced &&
		    enc->useMaxBitrate)
			enc->rateControl = H264RateControlConstrainedVBR;


		switch (enc->rateControl) {
		case H264RateControlCBR:
			return ApplyCBR(enc.get());
		case H264RateControlConstrainedVBR:
			return ApplyCVBR(enc.get());
		case H264RateControlVBR:
			return ApplyVBR(enc.get());
		case H264RateControlCQP:
			return ApplyCQP(enc.get());
		default: return false;
		}
	};

	if (!enc->h264Encoder->Initialize(applySettings))
		return nullptr;

	return enc.release();
}

static void MFH264_Destroy(void *data)
{
	MFH264_Encoder *enc = static_cast<MFH264_Encoder *>(data);
	delete enc;
}

static bool MFH264_Encode(void *data, struct encoder_frame *frame,
		struct encoder_packet *packet, bool *received_packet)
{
	MFH264_Encoder *enc = static_cast<MFH264_Encoder *>(data);
	Status status;

	*received_packet = false;

	if (!enc->h264Encoder->ProcessInput(frame->data, frame->linesize,
			frame->pts, &status))
		return false;

	UINT8 *outputData;
	UINT32 outputDataLength;
	UINT64 outputPts;
	UINT64 outputDts;
	bool keyframe;

	if (!enc->h264Encoder->ProcessOutput(&outputData, &outputDataLength,
			&outputPts, &outputDts, &keyframe, &status))
		return false;

	// Needs more input, not a failure case
	if (status == NEED_MORE_INPUT)
		return true;

	packet->type = OBS_ENCODER_VIDEO;
	packet->pts = outputPts;
	packet->dts = outputPts;
	packet->data = outputData;
	packet->size = outputDataLength;
	packet->keyframe = keyframe;

	*received_packet = true;
	return true;
}

static bool MFH264_GetExtraData(void *data, uint8_t **extra_data, size_t *size)
{
	MFH264_Encoder *enc = static_cast<MFH264_Encoder *>(data);

	uint8_t *extraData;
	UINT32 extraDataLength;


	if (!enc->h264Encoder->ExtraData(&extraData, &extraDataLength))
		return false;

	*extra_data = extraData;
	*size = extraDataLength;

	return true;
}

static bool MFH264_GetSEIData(void *data, uint8_t **sei_data, size_t *size)
{
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(sei_data);
	UNUSED_PARAMETER(size);

	return false;
}

static void MFH264_GetVideoInfo(void *, struct video_scale_info *info)
{
	info->format = VIDEO_FORMAT_NV12;
}

static bool MFH264_Update(void *data, obs_data_t *settings)
{
	MFH264_Encoder *enc = static_cast<MFH264_Encoder *>(data);

	UpdateParams(enc, settings);

	enc->h264Encoder->SetBitrate(enc->bitrate);
	enc->h264Encoder->SetQP(enc->qp);

	return true;
}

void RegisterMFH264Encoder()
{
	obs_encoder_info info = { 0 };
	info.id = "mf_h264";
	info.type = OBS_ENCODER_VIDEO;
	info.codec = "h264";
	info.get_name = MFH264_GetName;
	info.create = MFH264_Create;
	info.destroy = MFH264_Destroy;
	info.encode = MFH264_Encode;
	info.update = MFH264_Update;
	info.get_properties = MFH264_GetProperties;
	info.get_defaults = MFH264_GetDefaults;
	info.get_extra_data = MFH264_GetExtraData;
	info.get_sei_data = MFH264_GetSEIData;
	info.get_video_info = MFH264_GetVideoInfo;

	obs_register_encoder(&info);
}
