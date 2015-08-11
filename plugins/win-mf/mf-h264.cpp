#include <obs-module.h>

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
	uint32_t bitrate;
	uint32_t maxBitrate;
	bool useMaxBitrate;
	uint32_t bufferSize;
	bool useBufferSize;
	H264Profile profile;
	H264RateControl rateControl;
	H264QP qp;
	bool lowLatency;
	uint32_t bFrames;
};

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

	bool use_bufsize = obs_data_get_bool(settings, "use_bufsize");
	set_visible(ppts, "buffer_size", use_bufsize);

	return true;
}

static bool use_max_bitrate_modified(obs_properties_t *ppts, obs_property_t *p,
	obs_data_t *settings)
{
	UNUSED_PARAMETER(p);

	bool use_max_bitrate = obs_data_get_bool(settings, "use_max_bitrate");
	set_visible(ppts, "max_bitrate", use_max_bitrate);

	return true;
}

static bool rate_control_modified(obs_properties_t *ppts, obs_property_t *p,
	obs_data_t *settings)
{
	H264RateControl rateControl = (H264RateControl)obs_data_get_int(
		settings, "rate_control");

	set_visible(ppts, "bitrate", false);
	set_visible(ppts, "use_bufsize", false);
	set_visible(ppts, "buffer_size", false);
	set_visible(ppts, "use_max_bitrate", false);
	set_visible(ppts, "max_bit_rate", false);
	set_visible(ppts, "qpi", false);
	set_visible(ppts, "qpp", false);
	set_visible(ppts, "qpb", false);

	switch (rateControl) {
	case H264RateControlCBR:
		use_bufsize_modified(ppts, NULL, settings);
		use_max_bitrate_modified(ppts, NULL, settings);
		set_visible(ppts, "bitrate", true);
		set_visible(ppts, "use_bufsize", true);
		set_visible(ppts, "use_max_bitrate", true);
		break;
	case H264RateControlConstrainedVBR:
		use_bufsize_modified(ppts, NULL, settings);
		use_max_bitrate_modified(ppts, NULL, settings);
		set_visible(ppts, "bitrate", true);
		set_visible(ppts, "use_bufsize", true);
		set_visible(ppts, "use_max_bitrate", true);
		break;
	case H264RateControlVBR:
		use_bufsize_modified(ppts, NULL, settings);
		set_visible(ppts, "bitrate", true);
		set_visible(ppts, "use_bufsize", true);
		break;
	case H264RateControlCQP:
		set_visible(ppts, "qpi", true);
		set_visible(ppts, "qpp", true);
		set_visible(ppts, "qpb", true);
		break;
	default: break;
	}

	return true;
}

#define TEXT_ENCODER         obs_module_text("Encoder")
#define TEXT_LOW_LAT         obs_module_text("LowLatency")
#define TEXT_B_FRAMES        obs_module_text("BFrames")
#define TEXT_BITRATE         obs_module_text("Bitrate")
#define TEXT_CUSTOM_BUF      obs_module_text("CustomBufsize")
#define TEXT_BUF_SIZE        obs_module_text("BufferSize")
#define TEXT_USE_MAX_BITRATE obs_module_text("CustomMaxBitrate")
#define TEXT_MAX_BITRATE     obs_module_text("MaxBitrate")
#define TEXT_KEYINT_SEC      obs_module_text("KeyframeIntervalSec")
#define TEXT_RATE_CONTROL    obs_module_text("RateControl")
#define TEXT_QPI             obs_module_text("QPI")
#define TEXT_QPP             obs_module_text("QPP")
#define TEXT_QPB             obs_module_text("QPB")
#define TEXT_PROFILE         obs_module_text("Profile")
#define TEXT_NONE            obs_module_text("None")

static obs_properties_t *MFH264_GetProperties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *p = obs_properties_add_list(props, "encoder_guid", 
			TEXT_ENCODER, OBS_COMBO_TYPE_LIST,
			OBS_COMBO_FORMAT_STRING);
	auto encoders = MF::EncoderDescriptor::Enumerate();
	for (auto e : encoders) {
		obs_property_list_add_string(p, e->Name().c_str(), 
				e->GuidString().c_str());
	}

	obs_property_t *list = obs_properties_add_list(props, "profile",
		TEXT_PROFILE, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(list, "baseline", H264ProfileBaseline);
	obs_property_list_add_int(list, "main", H264ProfileMain);
	obs_property_list_add_int(list, "high", H264ProfileHigh);

	obs_properties_add_int(props, "keyint_sec", TEXT_KEYINT_SEC, 0, 20, 1);
	obs_properties_add_bool(props, "low_latency", TEXT_LOW_LAT);
	obs_properties_add_int(props, "b_frames", TEXT_B_FRAMES, 0, 16, 1);

	list = obs_properties_add_list(props, "rate_control",
		TEXT_RATE_CONTROL, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(list, "CBR", H264RateControlCBR);
	obs_property_list_add_int(list, "VBR", H264RateControlVBR);
	obs_property_list_add_int(list, "Constrained VBR",
		H264RateControlConstrainedVBR);
	obs_property_list_add_int(list, "CQP", H264RateControlCQP);

	obs_property_set_modified_callback(list, rate_control_modified);

	obs_properties_add_int(props, "bitrate", TEXT_BITRATE, 50, 10000000,
			1);

	p = obs_properties_add_bool(props, "use_bufsize", TEXT_CUSTOM_BUF);
	obs_property_set_modified_callback(p, use_bufsize_modified);
	obs_properties_add_int(props, "buffer_size", TEXT_BUF_SIZE, 0,
		10000000, 1);

	p = obs_properties_add_bool(props, "use_max_bitrate",
			TEXT_USE_MAX_BITRATE);
	obs_property_set_modified_callback(p, use_max_bitrate_modified);
	obs_properties_add_int(props, "max_bitrate", TEXT_MAX_BITRATE, 50,
		10000000, 1);

	obs_properties_add_int(props, "qpi", TEXT_QPI, 0, 51, 1);
	obs_properties_add_int(props, "qpp", TEXT_QPP, 0, 51, 1);
	obs_properties_add_int(props, "qpb", TEXT_QPB, 0, 51, 1);

	return props;
}

static void MFH264_GetDefaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "encoder_guid",
			"{6CA50344-051A-4DED-9779-A43305165E35}");
	obs_data_set_default_int(settings, "bitrate", 2500);
	obs_data_set_default_bool(settings, "low_latency", true);
	obs_data_set_default_int(settings, "b_frames", 2);
	obs_data_set_default_bool(settings, "use_bufsize", false);
	obs_data_set_default_int(settings, "buffer_size", 2500);
	obs_data_set_default_bool(settings, "use_max_bitrate", false);
	obs_data_set_default_int(settings, "max_bitrate", 2500);
	obs_data_set_default_int(settings, "keyint_sec", 2);
	obs_data_set_default_int(settings, "rate_control", H264RateControlCBR);
	obs_data_set_default_int(settings, "profile", H264ProfileMain);
	obs_data_set_default_int(settings, "qpi", 26);
	obs_data_set_default_int(settings, "qpp", 26);
	obs_data_set_default_int(settings, "qpb", 26);
}

static void UpdateParams(MFH264_Encoder *enc, obs_data_t *settings)
{
	video_t *video = obs_encoder_video(enc->encoder);
	const struct video_output_info *voi = video_output_get_info(video);

	enc->width = voi->width;
	enc->height = voi->height;
	enc->framerateNum = voi->fps_num;
	enc->framerateDen = voi->fps_den;

	std::shared_ptr<EncoderDescriptor> ed;

	std::string encoderGuid = obs_data_get_string(settings, 
			"encoder_guid");
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

	enc->profile = (H264Profile)obs_data_get_int(settings, "profile");


	enc->rateControl = (H264RateControl)obs_data_get_int(settings, 
			"rate_control");

	enc->keyint = (UINT32)obs_data_get_int(settings, "keyint_sec");

	enc->bitrate = (UINT32)obs_data_get_int(settings, "bitrate");

	enc->useBufferSize = obs_data_get_bool(settings, "use_bufsize");
	enc->bufferSize = (uint32_t)obs_data_get_int(settings, "buffer_size");
	
	enc->useMaxBitrate = obs_data_get_bool(settings, "use_max_bitrate");
	enc->maxBitrate = (uint32_t)obs_data_get_int(settings, "max_bitrate");

	enc->qp.defaultQp = (uint16_t)obs_data_get_int(settings, "qpi");
	enc->qp.i = (uint16_t)obs_data_get_int(settings, "qpi");
	enc->qp.p = (uint16_t)obs_data_get_int(settings, "qpp");
	enc->qp.b = (uint16_t)obs_data_get_int(settings, "qpb");

	enc->lowLatency = obs_data_get_bool(settings, "low_latency");
	enc->bFrames = (uint32_t)obs_data_get_int(settings, "b_frames");
}

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

	if (enc->useMaxBitrate)
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
	std::unique_ptr<MFH264_Encoder> enc(new MFH264_Encoder());
	enc->encoder = encoder;

	UpdateParams(enc.get(), settings);

	enc->h264Encoder.reset(new H264Encoder(encoder, 
			enc->descriptor->Guid(), 
			enc->descriptor->Async(),
			enc->width, 
			enc->height, 
			enc->framerateNum, 
			enc->framerateDen, 
			enc->profile,
			enc->bitrate));

	auto applySettings = [&]() {
		enc.get()->h264Encoder->SetRateControl(enc->rateControl);
		enc.get()->h264Encoder->SetKeyframeInterval(enc->keyint);
		enc.get()->h264Encoder->SetLowLatency(enc->lowLatency);
		enc.get()->h264Encoder->SetBFrameCount(enc->bFrames);

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
	UNUSED_PARAMETER(data);
	UNUSED_PARAMETER(settings);

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