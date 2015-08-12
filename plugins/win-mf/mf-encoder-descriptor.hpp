#pragma once

#define WIN32_MEAN_AND_LEAN
#include <Windows.h>
#undef WIN32_MEAN_AND_LEAN

#include <mfapi.h>
#include <mfidl.h>

#include <stdint.h>
#include <vector>

#include <util/windows/ComPtr.hpp>

namespace MF {
class EncoderDescriptor {
public:
	static std::vector<std::shared_ptr<EncoderDescriptor>> EncoderDescriptor::Enumerate();

public:
	EncoderDescriptor(ComPtr<IMFActivate> activate, const std::string &name, GUID &guid, const std::string &guidString, bool isAsync, bool isHardware)
		: activate(activate), name(name), guid(guid), guidString(guidString), isAsync(isAsync), isHardware(isHardware)
	{}

	EncoderDescriptor(const EncoderDescriptor &) = delete;

public:
	std::string Name() { return name; }
	ComPtr<IMFActivate> &Activator() { return activate; }
	GUID &Guid() { return guid; }
	std::string GuidString() { return guidString; }
	bool Async() { return isAsync; }
	bool Hardware() { return isHardware; }

private:
	ComPtr<IMFActivate> activate;
	std::string name;
	GUID guid;
	std::string guidString;
	bool isAsync;
	bool isHardware;
};
};
