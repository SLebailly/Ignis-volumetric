#include "LoaderMedium.h"
#include "Loader.h"
#include "LoaderUtils.h"
#include "Logger.h"
#include "ShadingTree.h"

namespace IG {

static void medium_homogeneous(std::ostream& stream, const std::string& name, const std::shared_ptr<Parser::Object>& medium, ShadingTree& tree)
{
    tree.beginClosure(name);

    tree.addColor("sigma_a", *medium, Vector3f::Zero(), true);
    tree.addColor("sigma_s", *medium, Vector3f::Zero(), true);
    tree.addNumber("g", *medium, 0, true);

    const std::string media_id = tree.currentClosureID();
    stream << tree.pullHeader()
           << "  let medium_" << media_id << " : MediumGenerator = @|ctx| { maybe_unused(ctx); make_homogeneous_medium(" << tree.getInline("sigma_a")
           << ", " << tree.getInline("sigma_s")
           << ", make_henyeygreenstein_phase(" << tree.getInline("g") << ")) };" << std::endl;

    tree.endClosure();
}

static void medium_heterogeneous(std::ostream& stream, const std::string& name, const std::shared_ptr<Parser::Object>& medium, ShadingTree& tree)
{
    // FIXME: The shading context is not available here! Texture & PExpr will produce errors
    tree.beginClosure(name);

    const auto filename = tree.context().handlePath(medium->property("filename").getString(), *medium);
    size_t res_id              = tree.context().registerExternalResource(filename);

    tree.addNumber("g", *medium, 0, true);
    
    Transformf transform = medium->property("transform").getTransform();
    transform.makeAffine();
    const Transformf transform_inv = transform.inverse();

    const Eigen::Matrix<float, 3, 4> trans_matrix     = transform.matrix().block<3, 4>(0, 0); //note: was 3x4 in LoaderEntity...? Why not 4x4?
    const Eigen::Matrix<float, 3, 4> trans_matrix_inv = transform_inv.matrix().block<3, 4>(0, 0);

    IG_LOG(L_DEBUG) << "Loading heterogeneous medium\n" << std::endl;

    const std::string media_id = tree.currentClosureID();
    stream << tree.pullHeader()
           << "  let medium_" << media_id << " = make_heterogeneous_medium( device.load_buffer_by_id(" << res_id << ")"
           << ", make_henyeygreenstein_phase(" << tree.getInline("g") << "), " << LoaderUtils::inlineMatrix34(trans_matrix) << ", " << LoaderUtils::inlineMatrix34(trans_matrix_inv) <<");" << std::endl;

    tree.endClosure();
}

// It is recommended to not define the medium, instead of using vacuum
static void medium_vacuum(std::ostream& stream, const std::string& name, const std::shared_ptr<Parser::Object>&, ShadingTree& tree)
{
    tree.beginClosure(name);

    const std::string media_id = tree.currentClosureID();
    stream << tree.pullHeader()
           << "  let medium_" << media_id << " : MediumGenerator = @|_ctx| make_vacuum_medium();" << std::endl;

    tree.endClosure();
}

using MediumLoader = void (*)(std::ostream&, const std::string&, const std::shared_ptr<Parser::Object>&, ShadingTree&);
static const struct {
    const char* Name;
    MediumLoader Loader;
} _generators[] = {
    { "homogeneous", medium_homogeneous },
    { "heterogeneous", medium_heterogeneous },
    { "constant", medium_homogeneous },
    { "vacuum", medium_vacuum },
    { "", nullptr }
};

std::string LoaderMedium::generate(ShadingTree& tree)
{
    std::stringstream stream;

    size_t counter = 0;
    for (const auto& pair : tree.context().Scene.media()) {
        const auto medium = pair.second;

        bool found = false;
        for (size_t i = 0; _generators[i].Loader; ++i) {
            if (_generators[i].Name == medium->pluginType()) {
                _generators[i].Loader(stream, pair.first, medium, tree);
                ++counter;
                found = true;
                break;
            }
        }
        if (!found)
            IG_LOG(L_ERROR) << "No medium type '" << medium->pluginType() << "' available" << std::endl;
    }

    if (counter != 0)
        stream << std::endl;

    stream << "  let media = @|id:i32| {" << std::endl
           << "    match(id) {" << std::endl;

    size_t counter2 = 0;
    for (const auto& pair : tree.context().Scene.media()) {
        const auto medium          = pair.second;
        const std::string media_id = tree.getClosureID(pair.first);
        stream << "      " << counter2 << " => medium_" << media_id
               << "," << std::endl;
        ++counter2;
    }

    stream << "    _ => @|_ctx : ShadingContext| make_vacuum_medium()" << std::endl;

    stream << "    }" << std::endl
           << "  };" << std::endl;

    return stream.str();
}

} // namespace IG