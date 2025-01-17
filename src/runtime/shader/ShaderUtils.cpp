#include "ShaderUtils.h"
#include "loader/LoaderBSDF.h"
#include "loader/LoaderEntity.h"
#include "loader/LoaderLight.h"
#include "loader/LoaderShape.h"
#include "loader/LoaderTechnique.h"
#include "loader/LoaderUtils.h"

#include <cctype>
#include <sstream>

namespace IG {
std::string ShaderUtils::constructDevice(const LoaderContext& ctx)
{
    std::stringstream stream;

    stream << "let spi = " << ShaderUtils::inlineSPI(ctx) << ";" << std::endl
           << "let render_config = make_render_config_from_settings(settings, spi);" << std::endl
           << "let device = ";

    if (ctx.Options.Target.isCPU()) {
        const bool compact = false; /*ctx.Options.Target.vectorWidth() >= 8;*/ // FIXME: Maybe something wrong with this flag?
        const bool single  = ctx.Options.Target.vectorWidth() >= 4;

        // TODO: Better decisions?
        std::string min_max = "make_default_min_max()";
        if (ctx.Options.Target.vectorWidth() >= 4)
            min_max = "make_cpu_int_min_max()";

        stream << "make_cpu_device("
               << "render_config, "
               << (compact ? "true" : "false") << ", "
               << (single ? "true" : "false") << ", "
               << min_max << ", "
               << ctx.Options.Target.vectorWidth()
               << ", settings.thread_count"
               << ", 16"
               << ", true);";
    } else {
        // TODO: Customize kernel config for device?
        switch (ctx.Options.Target.gpuArchitecture()) {
        case GPUArchitecture::AMD:
            stream << "make_amdgpu_device(settings.device, render_config, make_default_gpu_kernel_config());";
            break;
        default:
        case GPUArchitecture::Nvidia:
            stream << "make_nvvm_device(settings.device, render_config, make_default_gpu_kernel_config());";
            break;
        }
    }

    return stream.str();
}

std::string ShaderUtils::generateDatabase(const LoaderContext& ctx)
{
    std::stringstream stream;
    stream << "  let entities = load_entity_table(device); maybe_unused(entities);" << std::endl
           << generateShapeLookup(ctx)
           << "  maybe_unused(shapes);" << std::endl;
    return stream.str();
}

std::string ShaderUtils::generateShapeLookup(const LoaderContext& ctx)
{
    std::vector<ShapeProvider*> provs;
    provs.reserve(ctx.Shapes->providers().size());
    for (const auto& p : ctx.Shapes->providers())
        provs.emplace_back(p.second.get());

    if (provs.size() == 1)
        return generateShapeLookup("shapes", provs.front(), ctx);

    std::stringstream stream;
    stream << "  let shapes = load_shape_table(device, @|type_id, data| { match type_id {" << std::endl;

    for (size_t i = 0; i < provs.size() - 1; ++i)
        stream << "    " << provs.at(i)->id() << " => " << provs.at(i)->generateShapeCode(ctx) << "," << std::endl;

    stream << "    _ => " << provs.back()->generateShapeCode(ctx) << std::endl
           << "  }});" << std::endl;

    return stream.str();
}

std::string ShaderUtils::generateShapeLookup(const std::string& varname, ShapeProvider* provider, const LoaderContext& ctx)
{
    std::stringstream stream;
    stream << "  let " << varname << " = load_shape_table(device, @|_, data| { " << std::endl
           << provider->generateShapeCode(ctx) << std::endl
           << "  });" << std::endl;
    return stream.str();
}

std::string ShaderUtils::generateMaterialShader(ShadingTree& tree, size_t mat_id, bool requireLights, const std::string_view& output_var)
{
    std::stringstream stream;

    const Material material = tree.context().Materials.at(mat_id);
    stream << tree.context().BSDFs->generate(material.BSDF, tree);

    std::string bsdf_id = tree.getClosureID(material.BSDF);
    const bool isLight  = material.hasEmission() && tree.context().Lights->isAreaLight(material.Entity);

    if (material.hasMediumInterface())
        stream << "  let medium_interface = make_medium_interface(" << material.MediumInner << ", " << material.MediumOuter << ");" << std::endl;
    else
        stream << "  let medium_interface = no_medium_interface();" << std::endl;

    // We do not embed the actual material id into the shader, as this makes the shader unique without any major performance gain
    if (isLight && requireLights) {
        const size_t light_id = tree.context().Lights->getAreaLightID(material.Entity);
        stream << "  let " << output_var << " : MaterialShader = @|ctx| make_emissive_material(mat_id, bsdf_" << bsdf_id << "(ctx), medium_interface,"
               << " @finite_lights.get(" << light_id << "));" << std::endl
               << std::endl;
    } else {
        stream << "  let " << output_var << " : MaterialShader = @|ctx| make_material(mat_id, bsdf_" << bsdf_id << "(ctx), medium_interface);" << std::endl
               << std::endl;
    }

    return stream.str();
}

std::string ShaderUtils::beginCallback(const LoaderContext& ctx)
{
    std::stringstream stream;

    stream << "#[export] fn ig_callback_shader(settings: &Settings, iter: i32) -> () {" << std::endl
           << "  " << ShaderUtils::constructDevice(ctx) << std::endl
           << "  let scene_bbox = " << ShaderUtils::inlineSceneBBox(ctx) << "; maybe_unused(scene_bbox);" << std::endl;

    return stream.str();
}

std::string ShaderUtils::endCallback()
{
    return "}";
}

std::string ShaderUtils::inlineSPI(const LoaderContext& ctx)
{
    std::stringstream stream;

    if (ctx.Options.SamplesPerIteration == 1) // Hardcode this case as some optimizations might apply
        stream << ctx.Options.SamplesPerIteration << " : i32";
    else // Fallback to dynamic spi
        stream << "settings.spi";

    // We do not hardcode the spi as default to prevent recompilations if spi != 1
    return stream.str();
}

std::string ShaderUtils::inlineSceneBBox(const LoaderContext& ctx)
{
    IG_UNUSED(ctx);

    std::stringstream stream;
    stream << "make_bbox(registry::get_global_parameter_vec3(\"__scene_bbox_lower\", vec3_expand(0)), registry::get_global_parameter_vec3(\"__scene_bbox_upper\", vec3_expand(0)))";
    return stream.str();
}

std::string ShaderUtils::inlineScene(const LoaderContext& ctx, bool embed)
{
    if (embed) {
        std::stringstream stream;
        stream << "  let scene  = Scene {" << std::endl
               << "    num_entities  = " << ctx.Entities->entityCount() << "," << std::endl
               << "    num_materials = " << ctx.Materials.size() << "," << std::endl
               << "    shapes   = shapes," << std::endl
               << "    entities = entities," << std::endl
               << "  };" << std::endl;
        return stream.str();
    } else {
        std::stringstream stream;
        stream << "  let scene  = Scene {" << std::endl
               << "    num_entities  = registry::get_global_parameter_i32(\"__entity_count\", 0)," << std::endl
               << "    num_materials = registry::get_global_parameter_i32(\"__material_count\", 0)," << std::endl
               << "    shapes   = shapes," << std::endl
               << "    entities = entities," << std::endl
               << "  };" << std::endl;
        return stream.str();
    }
}

std::string ShaderUtils::inlinePayloadInfo(const LoaderContext& ctx)
{
    std::stringstream stream;

    stream << "PayloadInfo{ primary_count = "
           << ctx.CurrentTechniqueVariantInfo().PrimaryPayloadCount
           << ", secondary_count = " << ctx.CurrentTechniqueVariantInfo().SecondaryPayloadCount
           << " }";

    return stream.str();
}
} // namespace IG