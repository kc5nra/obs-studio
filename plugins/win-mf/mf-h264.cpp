#include <obs-module.h>

#include <memory>

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
	uint32_t bitrate;
	H264Profile profile;
	H264RateControl rateControl;
	H264QP qp;
};
static const char *MFH264_GetName()
{
	return obs_module_text("MFH264Enc");
}

#define TEXT_ENCODER      obs_module_text("Encoder")
#define TEXT_BITRATE      obs_module_text("Bitrate")
#define TEXT_RATE_CONTROL obs_module_text("RateControl")
#define TEXT_QP           obs_module_text("QP")
#define TEXT_QPI          obs_module_text("QPI")
#define TEXT_QPP          obs_module_text("QPP")
#define TEXT_QPB          obs_module_text("QPB")
#define TEXT_PROFILE      obs_module_text("Profile")
#define TEXT_NONE         obs_module_text("None")

static obs_properties_t *MFH264_GetProperties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *p = obs_properties_add_list(props, "encoder", 
			TEXT_ENCODER, OBS_COMBO_TYPE_LIST,
			OBS_COMBO_FORMAT_STRING);
	auto encoders = MF::EncoderDescriptor::Enumerate();
	for (auto e : encoders) {
		obs_property_list_add_string(p, e->Name().c_str(), 
				e->GuidString().c_str());
	}

	obs_properties_add_int(props, "bitrate", TEXT_BITRATE, 50, 10000000, 
			1);

	obs_property_t *list = obs_properties_add_list(props, "rate_control",
		TEXT_RATE_CONTROL, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(list, "CBR", H264RateControlCBR);
	obs_property_list_add_int(list, "VBR", H264RateControlVBR);
	obs_property_list_add_int(list, "Constrained VBR", 
			H264RateControlConstrainedVBR);
	obs_property_list_add_int(list, "CQP", H264RateControlCQP);

	list = obs_properties_add_list(props, "profile", 
		TEXT_PROFILE, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(list, TEXT_NONE, H264ProfileBaseline);
	obs_property_list_add_int(list, "baseline", H264ProfileBaseline);
	obs_property_list_add_int(list, "main", H264ProfileMain);
	obs_property_list_add_int(list, "high", H264ProfileHigh);

	obs_properties_add_int(props, "qp", TEXT_QP, 0, 51, 1);
	obs_properties_add_int(props, "qpi", TEXT_QPI, 0, 51, 1);
	obs_properties_add_int(props, "qpp", TEXT_QPP, 0, 51, 1);
	obs_properties_add_int(props, "qpb", TEXT_QPB, 0, 51, 1);

	return props;
}

static void MFH264_GetDefaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "encoder",
			"{6CA50344-051A-4DED-9779-A43305165E35}");
	obs_data_set_default_int(settings, "bitrate", 2500);
	obs_data_set_default_int(settings, "profile", H264ProfileBaseline);
	obs_data_set_default_int(settings, "rate_control", H264RateControlCBR);
	obs_data_set_default_int(settings, "qp", 26);
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

	enc->bitrate = (UINT32)obs_data_get_int(settings, "bitrate");
	enc->profile = (H264Profile)obs_data_get_int(settings, "profile");
	enc->rateControl = (H264RateControl)obs_data_get_int(settings, 
			"rate_control");
	
	enc->qp.defaultQp = (uint16_t)obs_data_get_int(settings, "qp");
	enc->qp.i = (uint16_t)obs_data_get_int(settings, "qpi");
	enc->qp.p = (uint16_t)obs_data_get_int(settings, "qpp");
	enc->qp.b = (uint16_t)obs_data_get_int(settings, "qpb");
	
}

static void *MFH264_Create(obs_data_t *settings, obs_encoder_t *encoder)
{
	std::unique_ptr<MFH264_Encoder> enc(new MFH264_Encoder());
	enc->encoder = encoder;

	UpdateParams(enc.get(), settings);

	enc->h264Encoder.reset(new H264Encoder(encoder, 
			enc->descriptor->Activator(), 
			enc->width, 
			enc->height, 
			enc->framerateNum, 
			enc->framerateDen, 
			enc->bitrate, 
			enc->profile, 
			enc->rateControl, 
			enc->qp));

	if (!enc->h264Encoder->Initialize())
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

	if (!enc->h264Encoder->ProcessOutput(&outputData, &outputDataLength, &outputPts,
		&outputDts, &keyframe, &status))
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
	return false;
}

static void MFH264_GetVideoInfo(void *, struct video_scale_info *info)
{
	info->format = VIDEO_FORMAT_NV12;
}

static bool MFH264_Update(void *data, obs_data_t *settings)
{
	return true;
}

extern "C" void RegisterMFH264Encoder()
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