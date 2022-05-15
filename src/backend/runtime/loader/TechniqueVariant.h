#pragma once

#include "IG_Config.h"

namespace IG {
enum class CallbackType {
    BeforeIteration = 0,
    AfterIteration,
    _COUNT
};

template <typename T>
struct TechniqueVariantBase {
    T RayGenerationShader;
    T MissShader;
    std::vector<T> HitShaders;
    std::vector<T> AdvancedShadowHitShaders;
    std::vector<T> AdvancedShadowMissShaders;
    std::array<T, (size_t)CallbackType::_COUNT> CallbackShaders{};
};

using TechniqueVariant          = TechniqueVariantBase<std::string>;
using TechniqueVariantShaderSet = TechniqueVariantBase<void*>;
} // namespace IG