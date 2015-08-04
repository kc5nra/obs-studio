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
	EncoderDescriptor(ComPtr<IMFActivate> activate, const std::string &name, const std::string &guid, bool isAsync, bool isHardware) 
		: activate(activate), name(name), guid(guid), isAsync(isAsync), isHardware(isHardware)
	{}

	EncoderDescriptor(const EncoderDescriptor &) = delete;

public:
	std::string Name() { return name; }

private:	
	ComPtr<IMFActivate> activate;
	std::string name;
	std::string guid;
	bool isAsync;
	bool isHardware;
};
};