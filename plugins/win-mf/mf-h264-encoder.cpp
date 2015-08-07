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
#include <comdef.h>
#include <util/windows/ComPtr.hpp>

#include <d3d9.h>
#include <dxva2api.h>\

#include "mf-common.hpp"
#include "mf-encoder-descriptor.hpp"

#define MF_LOG_COM(msg, hr) MF_LOG(LOG_ERROR, \
		msg " failed,  %S (0x%08lx)", \
		_com_error(hr).ErrorMessage(), hr)

#define HRC(r) \
	if(FAILED(hr = (r))) { \
		MF_LOG_COM(#r, hr); \
		goto fail; \
	}

#pragma once

// The following code enables you to view the contents of a media type while 
// debugging.

#include <strsafe.h>

LPCWSTR GetGUIDNameConst(const GUID& guid);
HRESULT GetGUIDName(const GUID& guid, WCHAR **ppwsz);

HRESULT LogAttributeValueByIndex(IMFAttributes *pAttr, DWORD index);
HRESULT SpecialCaseAttributeValue(GUID guid, const PROPVARIANT& var);

void DBGMSG(PCWSTR format, ...);

HRESULT LogMediaType(IMFMediaType *pType)
{
	UINT32 count = 0;

	HRESULT hr = pType->GetCount(&count);
	if (FAILED(hr))
	{
		return hr;
	}

	if (count == 0)
	{
		DBGMSG(L"Empty media type.\n");
	}

	for (UINT32 i = 0; i < count; i++)
	{
		hr = LogAttributeValueByIndex(pType, i);
		if (FAILED(hr))
		{
			break;
		}
	}
	return hr;
}

HRESULT LogAttributeValueByIndex(IMFAttributes *pAttr, DWORD index)
{
	WCHAR *pGuidName = NULL;
	WCHAR *pGuidValName = NULL;

	GUID guid = { 0 };

	PROPVARIANT var;
	PropVariantInit(&var);

	HRESULT hr = pAttr->GetItemByIndex(index, &guid, &var);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = GetGUIDName(guid, &pGuidName);
	if (FAILED(hr))
	{
		goto done;
	}

	DBGMSG(L"\t%s\t", pGuidName);

	hr = SpecialCaseAttributeValue(guid, var);
	if (FAILED(hr))
	{
		goto done;
	}
	if (hr == S_FALSE)
	{
		switch (var.vt)
		{
		case VT_UI4:
			DBGMSG(L"%d", var.ulVal);
			break;

		case VT_UI8:
			DBGMSG(L"%I64d", var.uhVal);
			break;

		case VT_R8:
			DBGMSG(L"%f", var.dblVal);
			break;

		case VT_CLSID:
			hr = GetGUIDName(*var.puuid, &pGuidValName);
			if (SUCCEEDED(hr))
			{
				DBGMSG(pGuidValName);
			}
			break;

		case VT_LPWSTR:
			DBGMSG(var.pwszVal);
			break;

		case VT_VECTOR | VT_UI1:
			DBGMSG(L"<<byte array>>");
			break;

		case VT_UNKNOWN:
			DBGMSG(L"IUnknown");
			break;

		default:
			DBGMSG(L"Unexpected attribute type (vt = %d)", var.vt);
			break;
		}
	}

done:
	DBGMSG(L"\n");
	CoTaskMemFree(pGuidName);
	CoTaskMemFree(pGuidValName);
	PropVariantClear(&var);
	return hr;
}

HRESULT GetGUIDName(const GUID& guid, WCHAR **ppwsz)
{
	HRESULT hr = S_OK;
	WCHAR *pName = NULL;

	LPCWSTR pcwsz = GetGUIDNameConst(guid);
	if (pcwsz)
	{
		size_t cchLength = 0;

		hr = StringCchLength(pcwsz, STRSAFE_MAX_CCH, &cchLength);
		if (FAILED(hr))
		{
			goto done;
		}

		pName = (WCHAR*)CoTaskMemAlloc((cchLength + 1) * sizeof(WCHAR));

		if (pName == NULL)
		{
			hr = E_OUTOFMEMORY;
			goto done;
		}

		hr = StringCchCopy(pName, cchLength + 1, pcwsz);
		if (FAILED(hr))
		{
			goto done;
		}
	}
	else
	{
		hr = StringFromCLSID(guid, &pName);
	}

done:
	if (FAILED(hr))
	{
		*ppwsz = NULL;
		CoTaskMemFree(pName);
	}
	else
	{
		*ppwsz = pName;
	}
	return hr;
}

void LogUINT32AsUINT64(const PROPVARIANT& var)
{
	UINT32 uHigh = 0, uLow = 0;
	Unpack2UINT32AsUINT64(var.uhVal.QuadPart, &uHigh, &uLow);
	DBGMSG(L"%d x %d", uHigh, uLow);
}

float OffsetToFloat(const MFOffset& offset)
{
	return offset.value + (static_cast<float>(offset.fract) / 65536.0f);
}

HRESULT LogVideoArea(const PROPVARIANT& var)
{
	if (var.caub.cElems < sizeof(MFVideoArea))
	{
		return MF_E_BUFFERTOOSMALL;
	}

	MFVideoArea *pArea = (MFVideoArea*)var.caub.pElems;

	DBGMSG(L"(%f,%f) (%d,%d)", OffsetToFloat(pArea->OffsetX), OffsetToFloat(pArea->OffsetY),
		pArea->Area.cx, pArea->Area.cy);
	return S_OK;
}

// Handle certain known special cases.
HRESULT SpecialCaseAttributeValue(GUID guid, const PROPVARIANT& var)
{
	if ((guid == MF_MT_FRAME_RATE) || (guid == MF_MT_FRAME_RATE_RANGE_MAX) ||
		(guid == MF_MT_FRAME_RATE_RANGE_MIN) || (guid == MF_MT_FRAME_SIZE) ||
		(guid == MF_MT_PIXEL_ASPECT_RATIO))
	{
		// Attributes that contain two packed 32-bit values.
		LogUINT32AsUINT64(var);
	}
	else if ((guid == MF_MT_GEOMETRIC_APERTURE) ||
		(guid == MF_MT_MINIMUM_DISPLAY_APERTURE) ||
		(guid == MF_MT_PAN_SCAN_APERTURE))
	{
		// Attributes that an MFVideoArea structure.
		return LogVideoArea(var);
	}
	else
	{
		return S_FALSE;
	}
	return S_OK;
}

void DBGMSG(PCWSTR format, ...)
{
	va_list args;
	va_start(args, format);

	WCHAR msg[MAX_PATH];

	if (SUCCEEDED(StringCbVPrintf(msg, sizeof(msg), format, args)))
	{
		OutputDebugString(msg);
	}
}

#ifndef IF_EQUAL_RETURN
#define IF_EQUAL_RETURN(param, val) if(val == param) return L#val
#endif

LPCWSTR GetGUIDNameConst(const GUID& guid)
{
	IF_EQUAL_RETURN(guid, MF_MT_MAJOR_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_MAJOR_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_SUBTYPE);
	IF_EQUAL_RETURN(guid, MF_MT_ALL_SAMPLES_INDEPENDENT);
	IF_EQUAL_RETURN(guid, MF_MT_FIXED_SIZE_SAMPLES);
	IF_EQUAL_RETURN(guid, MF_MT_COMPRESSED);
	IF_EQUAL_RETURN(guid, MF_MT_SAMPLE_SIZE);
	IF_EQUAL_RETURN(guid, MF_MT_WRAPPED_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_NUM_CHANNELS);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_SAMPLES_PER_SECOND);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_FLOAT_SAMPLES_PER_SECOND);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_AVG_BYTES_PER_SECOND);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_BLOCK_ALIGNMENT);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_BITS_PER_SAMPLE);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_VALID_BITS_PER_SAMPLE);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_SAMPLES_PER_BLOCK);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_CHANNEL_MASK);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_FOLDDOWN_MATRIX);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_PEAKREF);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_PEAKTARGET);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_AVGREF);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_WMADRC_AVGTARGET);
	IF_EQUAL_RETURN(guid, MF_MT_AUDIO_PREFER_WAVEFORMATEX);
	IF_EQUAL_RETURN(guid, MF_MT_AAC_PAYLOAD_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION);
	IF_EQUAL_RETURN(guid, MF_MT_FRAME_SIZE);
	IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE);
	IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE_RANGE_MAX);
	IF_EQUAL_RETURN(guid, MF_MT_FRAME_RATE_RANGE_MIN);
	IF_EQUAL_RETURN(guid, MF_MT_PIXEL_ASPECT_RATIO);
	IF_EQUAL_RETURN(guid, MF_MT_DRM_FLAGS);
	IF_EQUAL_RETURN(guid, MF_MT_PAD_CONTROL_FLAGS);
	IF_EQUAL_RETURN(guid, MF_MT_SOURCE_CONTENT_HINT);
	IF_EQUAL_RETURN(guid, MF_MT_VIDEO_CHROMA_SITING);
	IF_EQUAL_RETURN(guid, MF_MT_INTERLACE_MODE);
	IF_EQUAL_RETURN(guid, MF_MT_TRANSFER_FUNCTION);
	IF_EQUAL_RETURN(guid, MF_MT_VIDEO_PRIMARIES);
	IF_EQUAL_RETURN(guid, MF_MT_CUSTOM_VIDEO_PRIMARIES);
	IF_EQUAL_RETURN(guid, MF_MT_YUV_MATRIX);
	IF_EQUAL_RETURN(guid, MF_MT_VIDEO_LIGHTING);
	IF_EQUAL_RETURN(guid, MF_MT_VIDEO_NOMINAL_RANGE);
	IF_EQUAL_RETURN(guid, MF_MT_GEOMETRIC_APERTURE);
	IF_EQUAL_RETURN(guid, MF_MT_MINIMUM_DISPLAY_APERTURE);
	IF_EQUAL_RETURN(guid, MF_MT_PAN_SCAN_APERTURE);
	IF_EQUAL_RETURN(guid, MF_MT_PAN_SCAN_ENABLED);
	IF_EQUAL_RETURN(guid, MF_MT_AVG_BITRATE);
	IF_EQUAL_RETURN(guid, MF_MT_AVG_BIT_ERROR_RATE);
	IF_EQUAL_RETURN(guid, MF_MT_MAX_KEYFRAME_SPACING);
	IF_EQUAL_RETURN(guid, MF_MT_DEFAULT_STRIDE);
	IF_EQUAL_RETURN(guid, MF_MT_PALETTE);
	IF_EQUAL_RETURN(guid, MF_MT_USER_DATA);
	IF_EQUAL_RETURN(guid, MF_MT_AM_FORMAT_TYPE);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG_START_TIME_CODE);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG2_PROFILE);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG2_LEVEL);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG2_FLAGS);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG_SEQUENCE_HEADER);
	IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_SRC_PACK_0);
	IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_CTRL_PACK_0);
	IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_SRC_PACK_1);
	IF_EQUAL_RETURN(guid, MF_MT_DV_AAUX_CTRL_PACK_1);
	IF_EQUAL_RETURN(guid, MF_MT_DV_VAUX_SRC_PACK);
	IF_EQUAL_RETURN(guid, MF_MT_DV_VAUX_CTRL_PACK);
	IF_EQUAL_RETURN(guid, MF_MT_ARBITRARY_HEADER);
	IF_EQUAL_RETURN(guid, MF_MT_ARBITRARY_FORMAT);
	IF_EQUAL_RETURN(guid, MF_MT_IMAGE_LOSS_TOLERANT);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG4_SAMPLE_DESCRIPTION);
	IF_EQUAL_RETURN(guid, MF_MT_MPEG4_CURRENT_SAMPLE_ENTRY);
	IF_EQUAL_RETURN(guid, MF_MT_ORIGINAL_4CC);
	IF_EQUAL_RETURN(guid, MF_MT_ORIGINAL_WAVE_FORMAT_TAG);

	// Media types

	IF_EQUAL_RETURN(guid, MFMediaType_Audio);
	IF_EQUAL_RETURN(guid, MFMediaType_Video);
	IF_EQUAL_RETURN(guid, MFMediaType_Protected);
	IF_EQUAL_RETURN(guid, MFMediaType_SAMI);
	IF_EQUAL_RETURN(guid, MFMediaType_Script);
	IF_EQUAL_RETURN(guid, MFMediaType_Image);
	IF_EQUAL_RETURN(guid, MFMediaType_HTML);
	IF_EQUAL_RETURN(guid, MFMediaType_Binary);
	IF_EQUAL_RETURN(guid, MFMediaType_FileTransfer);

	IF_EQUAL_RETURN(guid, MFVideoFormat_AI44); //     FCC('AI44')
	IF_EQUAL_RETURN(guid, MFVideoFormat_ARGB32); //   D3DFMT_A8R8G8B8 
	IF_EQUAL_RETURN(guid, MFVideoFormat_AYUV); //     FCC('AYUV')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DV25); //     FCC('dv25')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DV50); //     FCC('dv50')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DVH1); //     FCC('dvh1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DVSD); //     FCC('dvsd')
	IF_EQUAL_RETURN(guid, MFVideoFormat_DVSL); //     FCC('dvsl')
	IF_EQUAL_RETURN(guid, MFVideoFormat_H264); //     FCC('H264')
	IF_EQUAL_RETURN(guid, MFVideoFormat_I420); //     FCC('I420')
	IF_EQUAL_RETURN(guid, MFVideoFormat_IYUV); //     FCC('IYUV')
	IF_EQUAL_RETURN(guid, MFVideoFormat_M4S2); //     FCC('M4S2')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MJPG);
	IF_EQUAL_RETURN(guid, MFVideoFormat_MP43); //     FCC('MP43')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MP4S); //     FCC('MP4S')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MP4V); //     FCC('MP4V')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MPG1); //     FCC('MPG1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MSS1); //     FCC('MSS1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_MSS2); //     FCC('MSS2')
	IF_EQUAL_RETURN(guid, MFVideoFormat_NV11); //     FCC('NV11')
	IF_EQUAL_RETURN(guid, MFVideoFormat_NV12); //     FCC('NV12')
	IF_EQUAL_RETURN(guid, MFVideoFormat_P010); //     FCC('P010')
	IF_EQUAL_RETURN(guid, MFVideoFormat_P016); //     FCC('P016')
	IF_EQUAL_RETURN(guid, MFVideoFormat_P210); //     FCC('P210')
	IF_EQUAL_RETURN(guid, MFVideoFormat_P216); //     FCC('P216')
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB24); //    D3DFMT_R8G8B8 
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB32); //    D3DFMT_X8R8G8B8 
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB555); //   D3DFMT_X1R5G5B5 
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB565); //   D3DFMT_R5G6B5 
	IF_EQUAL_RETURN(guid, MFVideoFormat_RGB8);
	IF_EQUAL_RETURN(guid, MFVideoFormat_UYVY); //     FCC('UYVY')
	IF_EQUAL_RETURN(guid, MFVideoFormat_v210); //     FCC('v210')
	IF_EQUAL_RETURN(guid, MFVideoFormat_v410); //     FCC('v410')
	IF_EQUAL_RETURN(guid, MFVideoFormat_WMV1); //     FCC('WMV1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_WMV2); //     FCC('WMV2')
	IF_EQUAL_RETURN(guid, MFVideoFormat_WMV3); //     FCC('WMV3')
	IF_EQUAL_RETURN(guid, MFVideoFormat_WVC1); //     FCC('WVC1')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y210); //     FCC('Y210')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y216); //     FCC('Y216')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y410); //     FCC('Y410')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y416); //     FCC('Y416')
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y41P);
	IF_EQUAL_RETURN(guid, MFVideoFormat_Y41T);
	IF_EQUAL_RETURN(guid, MFVideoFormat_YUY2); //     FCC('YUY2')
	IF_EQUAL_RETURN(guid, MFVideoFormat_YV12); //     FCC('YV12')
	IF_EQUAL_RETURN(guid, MFVideoFormat_YVYU);

	IF_EQUAL_RETURN(guid, MFAudioFormat_PCM); //              WAVE_FORMAT_PCM 
	IF_EQUAL_RETURN(guid, MFAudioFormat_Float); //            WAVE_FORMAT_IEEE_FLOAT 
	IF_EQUAL_RETURN(guid, MFAudioFormat_DTS); //              WAVE_FORMAT_DTS 
	IF_EQUAL_RETURN(guid, MFAudioFormat_Dolby_AC3_SPDIF); //  WAVE_FORMAT_DOLBY_AC3_SPDIF 
	IF_EQUAL_RETURN(guid, MFAudioFormat_DRM); //              WAVE_FORMAT_DRM 
	IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudioV8); //        WAVE_FORMAT_WMAUDIO2 
	IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudioV9); //        WAVE_FORMAT_WMAUDIO3 
	IF_EQUAL_RETURN(guid, MFAudioFormat_WMAudio_Lossless); // WAVE_FORMAT_WMAUDIO_LOSSLESS 
	IF_EQUAL_RETURN(guid, MFAudioFormat_WMASPDIF); //         WAVE_FORMAT_WMASPDIF 
	IF_EQUAL_RETURN(guid, MFAudioFormat_MSP1); //             WAVE_FORMAT_WMAVOICE9 
	IF_EQUAL_RETURN(guid, MFAudioFormat_MP3); //              WAVE_FORMAT_MPEGLAYER3 
	IF_EQUAL_RETURN(guid, MFAudioFormat_MPEG); //             WAVE_FORMAT_MPEG 
	IF_EQUAL_RETURN(guid, MFAudioFormat_AAC); //              WAVE_FORMAT_MPEG_HEAAC 
	IF_EQUAL_RETURN(guid, MFAudioFormat_ADTS); //             WAVE_FORMAT_MPEG_ADTS_AAC 

	return NULL;
}

class AsyncCallback : public IMFAsyncCallback
{
public:
	
	AsyncCallback(std::function<HRESULT(AsyncCallback *callback, ComPtr<IMFAsyncResult> res)> func_) :func(func_)
	{
	}

	STDMETHODIMP_(ULONG) AddRef() {
		return InterlockedIncrement(&count);
	}
	
	STDMETHODIMP_(ULONG) Release() {
		ULONG c = InterlockedDecrement(&count);
		if (c == 0) { 
			delete this; 
		}
		return c;
	}

	STDMETHODIMP QueryInterface(REFIID iid, void** ppv)
	{
		if (!ppv)
		{
			return E_POINTER;
		}
		if (iid == __uuidof(IUnknown))
		{
			*ppv = static_cast<IUnknown*>(static_cast<IMFAsyncCallback*>(this));
		}
		else if (iid == __uuidof(IMFAsyncCallback))
		{
			*ppv = static_cast<IMFAsyncCallback*>(this);
		}
		else
		{
			*ppv = NULL;
			return E_NOINTERFACE;
		}

		AddRef();

		return S_OK;
	}

	STDMETHODIMP GetParameters(DWORD*, DWORD*)
	{
		return E_NOTIMPL;
	}

	STDMETHODIMP Invoke(IMFAsyncResult* pAsyncResult)
	{
		return func(this, pAsyncResult);
	}

	volatile ULONG count;
	std::function<HRESULT(AsyncCallback *callbkac, ComPtr<IMFAsyncResult> res)> func;
};

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

struct mf_h264_frame {
	bool keyframe;
	uint64_t pts;
	uint64_t dts;
	size_t size;
	uint8_t *data;
};

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
	
	BYTE *extra_data = NULL;
	size_t extra_data_size = 0;
	
	BYTE *output;
	DWORD outputSize;
	
	const char* profile;

	std::vector<mf_h264_frame> outputFrames;

	volatile ULONG inputRequestCount = 0;
	volatile ULONG outputRequestCount = 0;
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
	HRC(i->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlaceMode::MFVideoInterlace_Progressive));
	HRC(MFSetAttributeRatio(i, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));

	HRC(o->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	HRC(o->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
	HRC(MFSetAttributeSize(o, MF_MT_FRAME_SIZE, enc->width, enc->height));
	HRC(MFSetAttributeRatio(o, MF_MT_FRAME_RATE, enc->fps_num, enc->fps_den));
	HRC(o->SetUINT32(MF_MT_AVG_BITRATE, enc->bitrate * 1000));
	HRC(o->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlaceMode::MFVideoInterlace_Progressive));
	HRC(MFSetAttributeRatio(o, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
	HRC(o->SetUINT32(MF_MT_MPEG2_LEVEL, -1));
	HRC(o->SetUINT32(MF_MT_MPEG2_PROFILE, MapProfile(enc->profile)));

	LogMediaType(i);
	LogMediaType(o);

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
	ComPtr<IMFAttributes> attr;
	ComPtr<IMFMediaEventGenerator> eventGenerator;
	ComPtr<AsyncCallback> callback;

	std::unique_ptr<mf_h264_encoder> enc(new mf_h264_encoder());

	enc->encoder = encoder;

	update_params(enc.get(), settings);

	auto i = MF::EncoderDescriptor::Enumerate();
	hr = i[0].get()->Activator()->ActivateObject(IID_PPV_ARGS(&transform));

	CreateMediaTypes(inputType, outputType, enc.get());

	hr = transform->GetAttributes(&attr);
	attr->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
	
	ComPtr<ICodecAPI> codec;
	hr = transform->QueryInterface(&codec);
	/*VARIANT v;
	v.vt = VT_UI4;
	v.ulVal = enc->bitrate * 1000;
	hr = codec->SetValue(&CODECAPI_AVEncCommonMeanBitRate, &v);
	v.vt = VT_UI4;
	v.ulVal = eAVEncCommonRateControlMode_CBR;
	hr = codec->SetValue(&CODECAPI_AVEncCommonRateControlMode, &v);
	v.vt = VT_BOOL;
	v.ulVal = TRUE;
	hr = codec->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &v);
	v.vt = VT_UI4;
	v.ulVal = 120;
	hr = codec->SetValue(&CODECAPI_AVEncMPVGOPSize, &v);
*/
	HRC(transform->SetOutputType(0, outputType, 0));
	HRC(transform->SetInputType(0, inputType, 0));
	
	HRC(transform->QueryInterface(&eventGenerator));

	mf_h264_encoder *h264_enc = enc.get();
	callback.Set(new AsyncCallback([&, eventGenerator, h264_enc](AsyncCallback *callback, ComPtr<IMFAsyncResult> res) {
		HRESULT hr;
		ComPtr<IMFMediaEvent> event;
		MediaEventType type;
		HRC(eventGenerator->EndGetEvent(res, &event));
		HRC(event->GetType(&type));
		if (type == METransformNeedInput) {
			InterlockedIncrement(&h264_enc->inputRequestCount);
		}
		else if (type == METransformHaveOutput) {
			InterlockedIncrement(&h264_enc->outputRequestCount);
		}
		HRC(eventGenerator->BeginGetEvent(callback, NULL));
	fail:
		return hr;
	}));

	eventGenerator->BeginGetEvent(callback, NULL);

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

	*extra_data = enc->extra_data;
	*size = enc->extra_data_size;
	return true;
fail:
	return false;
}

static HRESULT mf_h264_process_output(mf_h264_encoder *enc)
{
	HRESULT hr;
	ComPtr<IMFSample> sample;
	MFT_OUTPUT_STREAM_INFO outputInfo = { 0 };
	ComPtr<IMFMediaBuffer> mediaBuffer;
	BYTE *mediaBufferData;
	DWORD outputFlags, outputStatus = 0;
	MFT_OUTPUT_DATA_BUFFER output = { 0 };
	DWORD sampleFlags;
	DWORD bufferCount;
	INT64 samplePts;
	INT64 sampleDts;
	BYTE *data;
	DWORD dataSize;
	ComPtr<IMFMediaType> type;

	if (enc->outputRequestCount == 0)
		return S_OK;

	InterlockedDecrement(&enc->outputRequestCount);

	HRC(enc->transform->GetOutputStatus(&outputFlags));
	if (outputFlags != MFT_OUTPUT_STATUS_SAMPLE_READY)
		return true;

	//HRC(enc->transform->GetOutputStreamInfo(0, &outputInfo));
	//HRC(CreateEmptySample(sample, 10000000));
	//HRC(sample->GetBufferByIndex(0, &mediaBuffer));
	//HRC(sample->GetBufferCount(&bufferCount));

	output.pSample = NULL;
process_output:
	hr = enc->transform->ProcessOutput(0, 1, &output, &outputStatus);
	if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT || hr == MF_E_UNEXPECTED)
		return hr;

	if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
		HRC(enc->transform->GetOutputAvailableType(0, 0, &type));
		HRC(enc->transform->SetOutputType(0, type, 0));
		LogMediaType(type);
		goto process_output;
	}
	
	

	sample = output.pSample;
	HRC(sample->GetBufferByIndex(0, &mediaBuffer));

	bool isKeyFrame = !!MFGetAttributeUINT32(sample, MFSampleExtension_CleanPoint, FALSE);

	DWORD len;
	mediaBuffer->GetCurrentLength(&len);
	
	DWORD outSize;
	HRC(mediaBuffer->Lock(&mediaBufferData, NULL, &outSize));

	if (isKeyFrame && !enc->extra_data) {
		if (!enc->extra_data) {
			BYTE *ed;
			size_t es;
			mf_h264_extra_data(enc, &ed, &es);
		}
	}
	
	data = (BYTE *)bzalloc(outSize + enc->extra_data_size);
	dataSize = outSize;
	BYTE *out = data;
	if (isKeyFrame) {
		memcpy(out, enc->extra_data, enc->extra_data_size);
		out += enc->extra_data_size;
		dataSize += enc->extra_data_size;
	}
	//enc->outputBuffer.write((const char *)mediaBufferData, outSize);
	memcpy(out, mediaBufferData, outSize);
	HRC(mediaBuffer->Unlock());

	HRC(sample->GetSampleTime(&samplePts));
	sampleDts = MFGetAttributeUINT32(sample, MFSampleExtension_DecodeTimestamp, samplePts);

	mf_h264_frame f;
	f.data = data;
	f.size = dataSize;
	f.pts = samplePts;
	f.dts = sampleDts;
	f.keyframe = isKeyFrame;

	enc->outputFrames.emplace_back(f);
	
fail:
	return hr;
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
	while (hr == MF_E_NOTACCEPTING) {
		HRC(mf_h264_process_output(enc));
		hr = enc->transform->ProcessInput(0, sample, 0);
	}
	HRC(hr);

	sample.Release();
	mediaBuffer.Release();

	while (enc->outputRequestCount > 0) {
		mf_h264_process_output(enc);
	}

	if (!enc->outputFrames.empty()) {
		if (enc->output)
			bfree(enc->output);

		mf_h264_frame f = enc->outputFrames[0];
		
		packet->type = OBS_ENCODER_VIDEO;
		packet->pts = f.pts;
		packet->dts = f.dts;
		packet->data = f.data;
		packet->size = f.size;
		packet->keyframe = f.keyframe;
		
		enc->output = f.data;
		enc->outputSize = f.size;
		enc->outputFrames.erase(enc->outputFrames.begin());

		*received_packet = true;
	}

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