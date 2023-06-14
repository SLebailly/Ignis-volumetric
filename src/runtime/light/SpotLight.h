#pragma once

#include "Light.h"

namespace IG {
class SpotLight : public Light {
public:
    SpotLight(const std::string& name, const std::shared_ptr<SceneObject>& light);

    virtual bool isDelta() const override { return true; }
    virtual std::optional<Vector3f> position() const override { return mPosition; }
    virtual std::optional<Vector3f> direction() const override { return mDirection; }
    virtual float computeFlux(ShadingTree&) const override;

    virtual void serialize(const SerializationInput& input) const override;

    virtual std::optional<std::string> getEmbedClass() const override;
    virtual void embed(const EmbedInput& input) const override;

private:
    Vector3f mPosition;
    Vector3f mDirection;
    bool mUsingPower;

    std::shared_ptr<SceneObject> mLight;
};
} // namespace IG