#include <obs-module.h>

#include <memory>

#include "mf-h264-encoder.hpp"
#include "mf-encoder-descriptor.hpp"
#include <VersionHelpers.h>

using namespace MFH264;

static const char *MFH264_GetName()
{
	return obs_module_text("MFH264Enc");
}

static obs_properties_t *MFH264_GetProperties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *p = obs_properties_add_list(props, "encoder", "Encoder", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	auto encoders = MF::EncoderDescriptor::Enumerate();
	for (auto e : encoders) {
		obs_property_list_add_string(p, e->Name().c_str(), "");
	}

	obs_properties_add_int(props, "bitrate",
		obs_module_text("Bitrate"), 96, 192, 32);

	return props;
}

static void MFH264_GetDefaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "bitrate", 128);
}

static void *MFH264_Create(obs_data_t *settings, obs_encoder_t *encoder)
{
	std::unique_ptr<Encoder> enc(new Encoder(encoder));
	MF::EncoderDescriptor::Enumerate();
	return enc.release();
}

static void MFH264_Destroy(void *data)
{
	Encoder *enc = static_cast<Encoder *>(data);
	delete enc;
}

static bool MFH264_Encode(void *data, struct encoder_frame *frame,
struct encoder_packet *packet, bool *received_packet)
{
	Encoder *enc = static_cast<Encoder *>(data);
	*received_packet = false;
	return true;
}

static bool MFH264_GetExtraData(void *data, uint8_t **extra_data, size_t *size)
{
	Encoder *enc = static_cast<Encoder *>(data);
	return false;
}

static bool MFH264_GetSEIData(void *data, uint8_t **sei_data, size_t *size)
{
	return false;
}

static void MFH264_GetVideoInfo(void *, struct video_scale_info *info)
{
}

static bool MFH264_Update(void *data, obs_data_t *settings)
{
	return true;
}

//extern "C" void RegisterMFH264Encoder()
//{
//	obs_encoder_info info = { 0 };
//	info.id = "mf_h264";
//	info.type = OBS_ENCODER_VIDEO;
//	info.codec = "h264";
//	info.get_name = MFH264_GetName;
//	info.create = MFH264_Create;
//	info.destroy = MFH264_Destroy;
//	info.encode = MFH264_Encode;
//	info.update = MFH264_Update;
//	info.get_properties = MFH264_GetProperties;
//	info.get_defaults = MFH264_GetDefaults;
//	info.get_extra_data = MFH264_GetExtraData;
//	info.get_sei_data = MFH264_GetSEIData;
//	info.get_video_info = MFH264_GetVideoInfo;
//
//	obs_register_encoder(&info);
//}