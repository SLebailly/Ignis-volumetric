#pragma once

#include "Parser.h"
#include "Target.h"
#include "table/SceneDatabase.h"

namespace IG {
constexpr size_t DefaultAlignment = sizeof(float) * 4;

struct LoaderOptions {
	std::filesystem::path FilePath;
	Parser::Scene Scene;
	uint64 Configuration;
	std::string CameraType;
	std::string TechniqueType;
};

struct LoaderResult {
	SceneDatabase Database;
	float SceneRadius;
	std::string RayGenerationShader;
	std::string HitShader;
	std::string MissShader;
};

class Loader {
public:
	static bool load(const LoaderOptions& opts, LoaderResult& result);
};
} // namespace IG