#pragma once

#include <obs-module.h>

#define WIN32_MEAN_AND_LEAN
#include <Windows.h>
#undef WIN32_MEAN_AND_LEAN

#include <mfapi.h>
#include <mfidl.h>

#include <util/windows/ComPtr.hpp>

namespace MFH264 {

class Encoder {
public:
	Encoder(const obs_encoder_t *encoder)
	{}
};
}