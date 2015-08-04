#include <obs-module.h>
#include <util/platform.h>
#include <memory>

#include "mf-encoder-descriptor.hpp"

using namespace MF;

template<class T> class ComHeapPtr {

protected:
	T *ptr;

	inline void Kill()
	{
		if (ptr)
			CoTaskMemFree(ptr);
	}

	inline void Replace(T *p)
	{
		if (ptr != p) {
			if (ptr) ptr->Kill();
			ptr = p;
		}
	}

public:
	inline ComHeapPtr() : ptr(nullptr)                 {}
	inline ComHeapPtr(T *p) : ptr(p)                   {}
	inline ComHeapPtr(const ComHeapPtr<T> &c)          = delete;
	inline ComHeapPtr(ComHeapPtr<T> &&c)               = delete;
	inline ~ComHeapPtr()                               { Kill(); }

	inline void Clear()
	{
		if (ptr) {
			Kill();
			ptr = nullptr;
		}
	}

	inline ComPtr<T> &operator=(T *p)
	{
		Replace(p);
		return *this;
	}

	inline T *Detach()
	{
		T *out = ptr;
		ptr = nullptr;
		return out;
	}

	inline T **Assign()                { Clear(); return &ptr; }
	inline void Set(T *p)              { Kill(); ptr = p; }

	inline T *Get() const              { return ptr; }

	inline T **operator&()             { return Assign(); }

	inline    operator T*() const      { return ptr; }
	inline T *operator->() const       { return ptr; }

	inline bool operator==(T *p) const { return ptr == p; }
	inline bool operator!=(T *p) const { return ptr != p; }

	inline bool operator!() const      { return !ptr; }
};

static std::string MBSToString(wchar_t *mbs)
{
	char *cstr;
	os_wcs_to_utf8_ptr(mbs, 0, &cstr);
	std::string str = cstr;
	bfree(cstr);
	return str;
}

static std::unique_ptr<EncoderDescriptor> CreateDescriptor(ComPtr<IMFActivate> activate)
{
	HRESULT hr;
	ComHeapPtr<WCHAR> nameW;
	hr = activate->GetAllocatedString(
		MFT_FRIENDLY_NAME_Attribute,
		&nameW,
		NULL);

	std::string name = MBSToString(nameW);
	
	UINT32 flags;
	activate->GetUINT32(
		MF_TRANSFORM_FLAGS_Attribute,
		&flags);

	bool isAsync = !(flags & MFT_ENUM_FLAG_SYNCMFT);
	isAsync |= !!(flags & MFT_ENUM_FLAG_ASYNCMFT);
	bool isHardware = !!(flags & MFT_ENUM_FLAG_HARDWARE);

	GUID mftGuid = {0};
	activate->GetGUID(MFT_TRANSFORM_CLSID_Attribute, &mftGuid);
	ComHeapPtr<WCHAR> guidW;
	StringFromIID(mftGuid, &guidW);
	std::string guid = MBSToString(guidW);

	std::unique_ptr<EncoderDescriptor> descriptor(new EncoderDescriptor(activate, name, guid, isAsync, isHardware));
	return descriptor;
}

std::vector<std::shared_ptr<EncoderDescriptor>> EncoderDescriptor::Enumerate()
{
	HRESULT hr;
	UINT32 count = 0;
	std::vector<std::shared_ptr<EncoderDescriptor>> descriptors;

	ComHeapPtr<IMFActivate *> ppActivate;

	MFT_REGISTER_TYPE_INFO info = { MFMediaType_Video, MFVideoFormat_H264 };

	UINT32 unFlags = 0;
	
	unFlags |= MFT_ENUM_FLAG_LOCALMFT;
	unFlags |= MFT_ENUM_FLAG_TRANSCODE_ONLY;
	
	unFlags |= MFT_ENUM_FLAG_SYNCMFT;
	unFlags |= MFT_ENUM_FLAG_ASYNCMFT;
	unFlags |= MFT_ENUM_FLAG_HARDWARE;

	unFlags |= MFT_ENUM_FLAG_SORTANDFILTER;
	
	hr = MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,
		unFlags,
		NULL,
		&info,
		&ppActivate,
		&count);

	if (SUCCEEDED(hr) && count == 0)
	{
		return descriptors;
	}

	if (SUCCEEDED(hr))
	{
		for (decltype(count) i = 0; i < count; i++) {
			descriptors.emplace_back(std::move(CreateDescriptor(ppActivate[i])));
		}
	}

	for (UINT32 i = 0; i < count; i++)
	{
		ppActivate[i]->Release();
	}

	return descriptors;
}
