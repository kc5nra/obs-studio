#pragma once

#include <obs-module.h>

#define WIN32_MEAN_AND_LEAN
#include <Windows.h>
#undef WIN32_MEAN_AND_LEAN

#include <mfapi.h>
#include <mfidl.h>

#include <vector>
#include <queue>
#include <memory>
#include <atomic>

#include <util/windows/ComPtr.hpp>
#include "mf-encoder-descriptor.hpp"
#include "mf-common.hpp"

namespace MF {
	enum H264Profile {
		H264ProfileBaseline,
		H264ProfileMain,
		H264ProfileHigh
	};

	enum H264RateControl {
		H264RateControlCBR,
		H264RateControlConstrainedVBR,
		H264RateControlVBR,
		H264RateControlCQP
	};

	struct H264QP {
		uint16_t defaultQp;
		uint16_t i;
		uint16_t p;
		uint16_t b;

		uint64_t Pack() {
			return  (uint64_t)defaultQp |
				((uint64_t)i << 16) |
				((uint64_t)p << 32) |
				((uint64_t)b << 48);
		}
	};

	struct H264Frame {
	public:
		H264Frame(bool keyframe, uint64_t pts, uint64_t dts,
				std::unique_ptr<std::vector<uint8_t>> data)
			: keyframe(keyframe), pts(pts), dts(dts), 
			  data(std::move(data))
		{}
		bool Keyframe() { return keyframe; }
		BYTE *Data() { return data.get()->data(); }
		DWORD DataLength() { return (DWORD)data.get()->size(); }
		INT64 Pts() { return pts; }
		INT64 Dts() { return dts; }

	private:
		H264Frame(H264Frame const&) = delete;
		H264Frame& operator=(H264Frame const&) = delete;
	private:
		bool keyframe;
		INT64 pts;
		INT64 dts;
		std::unique_ptr<std::vector<uint8_t>> data;
	};

	class H264Encoder {
	public:
		H264Encoder(const obs_encoder_t *encoder, 
			ComPtr<IMFActivate> &activate,
			UINT32 width,
			UINT32 height,
			UINT32 framerateNum,
			UINT32 framerateDen,
			UINT32 bitrate,
			H264Profile profile,
			H264RateControl rateControl,
			H264QP &qp)
			: encoder(encoder),
			activate(activate),
			width(width),
			height(height),
			framerateNum(framerateNum),
			framerateDen(framerateDen),
			bitrate(bitrate),
			profile(profile),
			rateControl(rateControl),
			qp(qp)
		{
			extraData.clear();
		}

		bool Initialize();
		bool ProcessInput(UINT8 **data, UINT32 *linesize, UINT64 pts,
			Status *status);
		bool ProcessOutput(UINT8 **data, UINT32 *dataLength,
			UINT64 *pts, UINT64 *dts, bool *keyframe,
			Status *status);
		bool ExtraData(UINT8 **data, UINT32 *dataLength);

		const obs_encoder_t *ObsEncoder() { return encoder; }
		UINT32 Bitrate() { return bitrate; }
		H264Profile Profile() { return profile; }
		H264RateControl RateControl() { return rateControl; }
		H264QP QP() { return qp; }

	private:
		H264Encoder(H264Encoder const&) = delete;
		H264Encoder& operator=(H264Encoder const&) = delete;

	private:
		HRESULT InitializeEventGenerator();
		HRESULT InitializeExtraData();
		HRESULT CreateMediaTypes(ComPtr<IMFMediaType> &inputType,
			ComPtr<IMFMediaType> &outputType);
		HRESULT EnsureCapacity(ComPtr<IMFSample> &sample, DWORD length);
		HRESULT CreateEmptySample(ComPtr<IMFSample> &sample,
			ComPtr<IMFMediaBuffer> &buffer, DWORD length);

		HRESULT ProcessOutput();

	private:
		const obs_encoder_t *encoder;
		ComPtr<IMFActivate> activate;
		const UINT32 width;
		const UINT32 height;
		const UINT32 framerateNum;
		const UINT32 framerateDen;
		UINT32 bitrate;
		const H264Profile profile;
		const H264RateControl rateControl;
		H264QP qp;

		ComPtr<IMFTransform> transform;
		std::queue<std::unique_ptr<H264Frame>> encodedFrames;
		std::unique_ptr<H264Frame> activeFrame;
		std::vector<BYTE> extraData;
		std::atomic<UINT32> inputRequests;
		std::atomic<UINT32> outputRequests;
	};
}
