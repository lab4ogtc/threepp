
#include "threepp/materials/ShaderMaterial.hpp"

#include "threepp/renderers/shaders/ShaderChunk.hpp"

using namespace threepp;

ShaderMaterial::ShaderMaterial()
    : MaterialWithClipping(false),
      MaterialWithLights(false),
      MaterialWithWireframe(false, 1),
      MaterialWithLineWidth(1),
      vertexShader(shaders::ShaderChunk::instance().default_vertex()),
      fragmentShader(shaders::ShaderChunk::instance().default_fragment()) {

    this->fog = false;
    this->lights = false;
    this->clipping = false;

    defaultAttributeValues["color"] = Color(1, 1, 1);
    defaultAttributeValues["uv"] = Vector2(0, 0);
    defaultAttributeValues["uv2"] = Vector2(0, 0);
}


std::string ShaderMaterial::type() const {

    return "ShaderMaterial";
}

std::shared_ptr<ShaderMaterial> ShaderMaterial::create() {

    return std::shared_ptr<ShaderMaterial>(new ShaderMaterial());
}

std::shared_ptr<Material> ShaderMaterial::createDefault() const {

    return std::shared_ptr<ShaderMaterial>(new ShaderMaterial());
}

void ShaderMaterial::copyInto(Material& material) const {

    Material::copyInto(material);

    auto* m = material.as<ShaderMaterial>();
    m->vertexShader = vertexShader;
    m->fragmentShader = fragmentShader;
    m->uniforms = uniforms;
    m->shaderLanguage = shaderLanguage;
    m->uniformLayout = uniformLayout;
    m->customTextures = customTextures;
    m->index0AttributeName = index0AttributeName;
    m->uniformsNeedUpdate = uniformsNeedUpdate;
}
