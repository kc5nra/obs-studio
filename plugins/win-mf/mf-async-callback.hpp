#pragma once

#include <mfapi.h>
#include <functional>
#include <atomic>

#include <util/windows/ComPtr.hpp>

namespace MF {
class AsyncCallback : public IMFAsyncCallback
{
public:

	AsyncCallback(std::function<HRESULT(AsyncCallback *callback, 
			ComPtr<IMFAsyncResult> res)> func_) 
		: func(func_)
	{}

	STDMETHODIMP_(ULONG) AddRef() {
		return count.fetch_add(1);
	}

	STDMETHODIMP_(ULONG) Release() {
		ULONG c = count.fetch_sub(1);
		if (c == 0)
			delete this;
		return c;
	}

	STDMETHODIMP QueryInterface(REFIID iid, void** ppv)
	{
		if (!ppv)
			return E_POINTER;

		if (iid == __uuidof(IUnknown))
			*ppv = static_cast<IUnknown*>(
					static_cast<IMFAsyncCallback*>(this));
		else if (iid == __uuidof(IMFAsyncCallback))
			*ppv = static_cast<IMFAsyncCallback*>(this);
		else
			return E_NOINTERFACE;

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

private:
	std::atomic<ULONG> count;
	std::function<HRESULT(AsyncCallback *callbkac, ComPtr<IMFAsyncResult> res)> func;
};
}