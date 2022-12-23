#pragma once

#include "profiler.h"

struct ScopeProfiler {
	const char *name;
	bool enabled = true;

	ScopeProfiler(const char *name,
		      const struct profile_source_location_data *data = NULL)
		: name(name)
	{
		profile_start_with_info(name, data);
	}

	~ScopeProfiler() { Stop(); }

	ScopeProfiler(const ScopeProfiler &) = delete;
	ScopeProfiler(ScopeProfiler &&other)
		: name(other.name), enabled(other.enabled)
	{
		other.enabled = false;
	}

	ScopeProfiler &operator=(const ScopeProfiler &) = delete;
	ScopeProfiler &operator=(ScopeProfiler &&other) = delete;

	void Stop()
	{
		if (!enabled)
			return;

		profile_end(name);
		enabled = false;
	}
};

#ifndef NO_PROFILER_MACROS

#define ProfileScope_Name(x) OBS_PROFILE_CONCAT(x, __LINE__)

#define ProfileScope(name)                             \
	PROFILE_LOCATION(NULL);                        \
	ScopeProfiler ProfileScope_Name(ScopeProfiler) \
	{                                              \
		name, &PROFILE_LOCATION_NAME           \
	}

#define ProfileScopeNamed(name, var)         \
	PROFILE_LOCATION(NULL);              \
	ScopeProfiler var                    \
	{                                    \
		name, &PROFILE_LOCATION_NAME \
	}

#endif
