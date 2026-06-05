#include "threepp/textures/DataArrayTexture.hpp"

using namespace threepp;

DataArrayTexture::DataArrayTexture(
        ImageData data,
        unsigned int width,
        unsigned int height,
        unsigned int depth,
        Format format,
        Type type)
    : Texture({Image(std::move(data), width, height, depth)}) {

    this->format = format;
    this->type = type;
    this->magFilter = Filter::Nearest;
    this->minFilter = Filter::Nearest;
    this->generateMipmaps = false;
    this->unpackAlignment = 1;
    this->needsUpdate();
}

std::shared_ptr<DataArrayTexture> DataArrayTexture::create(
        const ImageData& data,
        unsigned int width,
        unsigned int height,
        unsigned int depth,
        Format format,
        Type type) {

    return std::shared_ptr<DataArrayTexture>(new DataArrayTexture(data, width, height, depth, format, type));
}

std::shared_ptr<DataArrayTexture> DataArrayTexture::create(
        const std::vector<unsigned char>& data,
        unsigned int width,
        unsigned int height,
        unsigned int depth) {

    return create(ImageData{data}, width, height, depth, Format::RGBA, Type::UnsignedByte);
}

std::shared_ptr<DataArrayTexture> DataArrayTexture::create(
        const std::vector<float>& data,
        unsigned int width,
        unsigned int height,
        unsigned int depth) {

    return create(ImageData{data}, width, height, depth, Format::RGBA, Type::Float);
}

std::shared_ptr<DataArrayTexture> DataArrayTexture::create(
        const std::vector<std::uint32_t>& data,
        unsigned int width,
        unsigned int height,
        unsigned int depth) {

    return create(ImageData{data}, width, height, depth, Format::RGBAInteger, Type::UnsignedInt);
}
