#pragma once

#include "LoaderContext.h"

namespace IG {
struct HitShader {
	static std::string setup(int entity_id, LoaderContext& ctx);
};
} // namespace IG