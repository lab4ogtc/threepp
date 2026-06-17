#ifndef THREEPP_DATAARRAYTEXTURE_HPP
#define THREEPP_DATAARRAYTEXTURE_HPP

#include "threepp/textures/Texture.hpp"

#include <cstdint>

namespace threepp {

    class DataArrayTexture: public Texture {

    public:
        static std::shared_ptr<DataArrayTexture> create(
                const ImageData& data,
                unsigned int width,
                unsigned int height,
                unsigned int depth,
                Format format = Format::RGBA,
                Type type = Type::UnsignedByte);

        static std::shared_ptr<DataArrayTexture> create(
                const std::vector<unsigned char>& data,
                unsigned int width,
                unsigned int height,
                unsigned int depth);

        static std::shared_ptr<DataArrayTexture> create(
                const std::vector<float>& data,
                unsigned int width,
                unsigned int height,
                unsigned int depth);

        static std::shared_ptr<DataArrayTexture> create(
                const std::vector<std::uint32_t>& data,
                unsigned int width,
                unsigned int height,
                unsigned int depth);

    private:
        DataArrayTexture(
                ImageData data,
                unsigned int width,
                unsigned int height,
                unsigned int depth,
                Format format,
                Type type);
    };

}// namespace threepp

#endif//THREEPP_DATAARRAYTEXTURE_HPP
