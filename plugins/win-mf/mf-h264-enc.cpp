#include "mf-common.hpp"
#include "mf-h264-encoder.hpp"
#include "mf-async-callback.hpp"

#include <codecapi.h>
#include <mferror.h>

using namespace MF;

static eAVEncH264VProfile MapProfile(H264Profile profile)
{
	switch (profile) {
	case H264ProfileBaseline: return eAVEncH264VProfile_Base;
	case H264ProfileMain:     return eAVEncH264VProfile_Main;
	case H264ProfileHigh:     return eAVEncH264VProfile_High;
	default:                  return eAVEncH264VProfile_Base;
	}
}

HRESULT H264Encoder::CreateMediaTypes(ComPtr<IMFMediaType> &i,
		ComPtr<IMFMediaType> &o)
{
	HRESULT hr;
	HRC(MFCreateMediaType(&i));
	HRC(MFCreateMediaType(&o));

	HRC(i->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	HRC(i->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
	HRC(MFSetAttributeSize(i, MF_MT_FRAME_SIZE, width, height));
	HRC(MFSetAttributeRatio(i, MF_MT_FRAME_RATE, framerateNum, 
			framerateDen));
	HRC(i->SetUINT32(MF_MT_INTERLACE_MODE, 
			MFVideoInterlaceMode::MFVideoInterlace_Progressive));
	HRC(MFSetAttributeRatio(i, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));

	HRC(o->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	HRC(o->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
	HRC(MFSetAttributeSize(o, MF_MT_FRAME_SIZE, width, height));
	HRC(MFSetAttributeRatio(o, MF_MT_FRAME_RATE, framerateNum, 
			framerateDen));
	HRC(o->SetUINT32(MF_MT_AVG_BITRATE, bitrate * 1000));
	HRC(o->SetUINT32(MF_MT_INTERLACE_MODE, 
			MFVideoInterlaceMode::MFVideoInterlace_Progressive));
	HRC(MFSetAttributeRatio(o, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
	HRC(o->SetUINT32(MF_MT_MPEG2_LEVEL, (UINT32)-1));
	HRC(o->SetUINT32(MF_MT_MPEG2_PROFILE, MapProfile(profile)));

	return S_OK;
fail:
	return hr;
}

HRESULT H264Encoder::InitializeEventGenerator()
{
	HRESULT hr;
	ComPtr<IMFMediaEventGenerator> eventGenerator;
	ComPtr<AsyncCallback> callback;

	HRC(transform->QueryInterface(&eventGenerator));

	callback.Set(new AsyncCallback([&, this, eventGenerator](
				AsyncCallback *callback, 
				ComPtr<IMFAsyncResult> res) {
		HRESULT hr, eventStatus;
		ComPtr<IMFMediaEvent> event;
		MediaEventType type;
		HRC(eventGenerator->EndGetEvent(res, &event));
		HRC(event->GetType(&type));
		HRC(event->GetStatus(&eventStatus));
		
		if (SUCCEEDED(eventStatus)) {
			if (type == METransformNeedInput) {
				inputRequests++;
				blog(LOG_INFO, "NeedInput!");
			}
			else if (type == METransformHaveOutput) {
				outputRequests++;
				blog(LOG_INFO, "NeedOutput!");
			}
		}

		HRC(eventGenerator->BeginGetEvent(callback, NULL));
	
	fail:
		return hr;
	}));

	eventGenerator->BeginGetEvent(callback, NULL);

	return S_OK;

fail:
	return hr;
}

HRESULT H264Encoder::InitializeExtraData()
{
	HRESULT hr;
	ComPtr<IMFMediaType> inputType;
	UINT32 headerSize;

	extraData.clear();

	HRC(transform->GetOutputCurrentType(0, &inputType));

	HRC(inputType->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &headerSize));
	
	extraData.resize(headerSize);
	
	HRC(inputType->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, extraData.data(), 
			headerSize, NULL));

	return S_OK;

fail:
	return hr;
}

bool H264Encoder::Initialize()
{
	HRESULT hr;

	ComPtr<IMFTransform> transform_;
	ComPtr<IMFMediaType> inputType, outputType;
	ComPtr<IMFAttributes> transformAttributes;
	MFT_OUTPUT_STREAM_INFO streamInfo = {0};
	HRC(activate->ActivateObject(IID_PPV_ARGS(&transform_)));

	HRC(CreateMediaTypes(inputType, outputType));

	if (async) {
		HRC(transform_->GetAttributes(&transformAttributes));
		HRC(transformAttributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, 
				TRUE));
	}
	
	MF_LOG(LOG_INFO, "Setting output type to transform");
	LogMediaType(outputType.Get());
	HRC(transform_->SetOutputType(0, outputType.Get(), 0));
	
	MF_LOG(LOG_INFO, "Setting input type to transform");
	LogMediaType(inputType.Get());
	HRC(transform_->SetInputType(0, inputType.Get(), 0));
	
	HRC(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,
		NULL));
	HRC(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,
		NULL));

	transform = transform_;

	if (async)
		HRC(InitializeEventGenerator());

	HRC(transform->GetOutputStreamInfo(0, &streamInfo));
	createOutputSample = !(streamInfo.dwFlags & 
			       (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
			        MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES));

	return true;

fail:
	return false;
}

bool H264Encoder::ExtraData(UINT8 **data, UINT32 *dataLength)
{
	if (extraData.empty())
		return false;

	*data = extraData.data();
	*dataLength = (UINT32)extraData.size();

	return true;
}

HRESULT H264Encoder::CreateEmptySample(ComPtr<IMFSample> &sample,
	ComPtr<IMFMediaBuffer> &buffer, DWORD length)
{
	HRESULT hr;

	HRC(MFCreateSample(&sample));
	HRC(MFCreateMemoryBuffer(length, &buffer));
	HRC(sample->AddBuffer(buffer.Get()));
	return S_OK;

fail:
	return hr;
}

HRESULT H264Encoder::EnsureCapacity(ComPtr<IMFSample> &sample, DWORD length)
{
	HRESULT hr;
	ComPtr<IMFMediaBuffer> buffer;
	DWORD currentLength;

	if (!sample) {
		HRC(CreateEmptySample(sample, buffer, length));
	}
	else {
		HRC(sample->GetBufferByIndex(0, &buffer));
	}

	HRC(buffer->GetMaxLength(&currentLength));
	if (currentLength < length) {
		HRC(sample->RemoveAllBuffers());
		HRC(MFCreateMemoryBuffer(length, &buffer));
		HRC(sample->AddBuffer(buffer));
	}
	else {
		buffer->SetCurrentLength(0);
	}

	return S_OK;

fail:
	return hr;
}

HRESULT H264Encoder::ProcessInput(ComPtr<IMFSample> sample)
{
	HRESULT hr = S_OK;
	if (async) {
		if (inputRequests == 1 && inputSamples.empty()) {
			inputRequests--;
			return transform->ProcessInput(0, sample, 0);
		}

		inputSamples.push(sample);

		while (inputRequests > 0) {
			if (inputSamples.empty())
				return hr;
			ComPtr<IMFSample> queuedSample = inputSamples.front();
			inputSamples.pop();
			inputRequests--;
			HRC(transform->ProcessInput(0, queuedSample, 0));
		}
	} else {
		return transform->ProcessInput(0, sample, 0);
	}

fail:
	return hr;
}

bool H264Encoder::ProcessInput(UINT8 **data, UINT32 *linesize, UINT64 pts, 
		Status *status)
{
	HRESULT hr;
	ComPtr<IMFSample> sample;
	ComPtr<IMFMediaBuffer> buffer;
	BYTE *bufferData;
	UINT64 sampleDur;
	UINT32 imageSize;

	HRC(MFCalculateImageSize(MFVideoFormat_NV12, width, height, &imageSize));

	HRC(CreateEmptySample(sample, buffer, imageSize));

	HRC(buffer->Lock(&bufferData, NULL, NULL));
	ProcessNV12([&, this](size_t height, int plane) {
		size_t l = linesize[plane] * height;
		memcpy(bufferData, data[plane], l);
		bufferData += l;
	}, height);
	HRC(buffer->Unlock());
	HRC(buffer->SetCurrentLength(imageSize));

	MFFrameRateToAverageTimePerFrame(framerateNum, framerateDen, &sampleDur);

	HRC(sample->SetSampleTime(pts * sampleDur));
	HRC(sample->SetSampleDuration(sampleDur));

	hr = ProcessInput(sample);

	while (hr == MF_E_NOTACCEPTING) {
		hr = ProcessOutput();
		if (SUCCEEDED(hr))
			hr = ProcessInput(sample);
	}
	if (FAILED(hr)) {
		MF_LOG_COM("transform->ProcessInput()", hr);
		return false;
	}

	*status = SUCCESS;
	return true;

fail:
	*status = FAILURE;
	return false;
}

HRESULT H264Encoder::ProcessOutput()
{
	HRESULT hr;
	ComPtr<IMFSample> sample;
	MFT_OUTPUT_STREAM_INFO outputInfo = { 0 };
	
	DWORD outputFlags, outputStatus = 0;
	MFT_OUTPUT_DATA_BUFFER output = { 0 };
	ComPtr<IMFMediaBuffer> buffer;
	BYTE *bufferData;
	DWORD bufferLength;
	INT64 samplePts;
	INT64 sampleDts;
	INT64 sampleDur;
	std::unique_ptr<std::vector<BYTE>> data(new std::vector<BYTE>());
	ComPtr<IMFMediaType> type;
	std::unique_ptr<H264Frame> frame;

	if (async) {
		if (outputRequests == 0)
			return S_OK;

		outputRequests--;
	}

	HRC(transform->GetOutputStatus(&outputFlags));
	if (outputFlags != MFT_OUTPUT_STATUS_SAMPLE_READY)
		return S_OK;

	
	if (createOutputSample) {
		HRC(transform->GetOutputStreamInfo(0, &outputInfo));
		HRC(CreateEmptySample(sample, buffer, outputInfo.cbSize));
		output.pSample = sample;
	} else {
		output.pSample = NULL;
	}

	while (true) {
		hr = transform->ProcessOutput(0, 1, &output, 
				&outputStatus);
		if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
			return hr;

		if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
			HRC(transform->GetOutputAvailableType(0, 0, &type));
			HRC(transform->SetOutputType(0, type, 0));
			MF_LOG(LOG_INFO, "Updating output type to transform");
			LogMediaType(type);
			if (async && outputRequests > 0) {
				outputRequests--;
				continue;
			} else {
				return MF_E_TRANSFORM_NEED_MORE_INPUT;
			}
		}

		if (hr != S_OK) {
			MF_LOG_COM("transform->ProcessOutput()", hr);
			return hr;
		}

		break;
	}

	sample = output.pSample;
	HRC(sample->GetBufferByIndex(0, &buffer));

	bool keyframe = !!MFGetAttributeUINT32(sample, 
			MFSampleExtension_CleanPoint, FALSE);

	HRC(buffer->Lock(&bufferData, NULL, &bufferLength));

	if (keyframe && extraData.empty())
		HRC(InitializeExtraData());

	data->reserve(bufferLength + extraData.size());
	
	if (keyframe)
		data->insert(data->end(), extraData.begin(), extraData.end()); 
	
	data->insert(data->end(), &bufferData[0], &bufferData[bufferLength]);
	HRC(buffer->Unlock());
	
	HRC(sample->GetSampleDuration(&sampleDur));
	HRC(sample->GetSampleTime(&samplePts));

	sampleDts = MFGetAttributeUINT64(sample, 
			MFSampleExtension_DecodeTimestamp, samplePts);

	frame.reset(new H264Frame(keyframe, 
			samplePts / sampleDur, 
			sampleDts / sampleDur, 
			std::move(data)));

	encodedFrames.push(std::move(frame));

	return S_OK;

fail:
	return hr;
}

bool H264Encoder::ProcessOutput(UINT8 **data, UINT32 *dataLength,
		UINT64 *pts, UINT64 *dts, bool *keyframe, Status *status)
{
	HRESULT hr;

	hr = ProcessOutput();

	if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT || encodedFrames.empty()) {
		*status = NEED_MORE_INPUT;
		return true;
	}

	if (FAILED(hr) && encodedFrames.empty()) {
		*status = FAILURE;
		return false;
	}

	activeFrame = std::move(encodedFrames.front());
	encodedFrames.pop();
	
	*data = activeFrame.get()->Data();
	*dataLength = activeFrame.get()->DataLength();
	*pts = activeFrame.get()->Pts();
	*dts = activeFrame.get()->Dts();
	*keyframe = activeFrame.get()->Keyframe();
	*status = SUCCESS;

	return true;
}