#pragma once

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
