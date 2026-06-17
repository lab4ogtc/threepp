
#ifndef THREEPP_DATATEXTURE_HPP
#define THREEPP_DATATEXTURE_HPP

#include "threepp/textures/Texture.hpp"

namespace threepp {

    class DataTexture: public Texture {

    public:

        void setData(ImageData data) {

            image().setData(std::move(data));
        }

        template<class T = unsigned char>
        static std::shared_ptr<DataTexture> create(
                int channels,
                unsigned int width, unsigned int height) {

            return std::shared_ptr<DataTexture>(new DataTexture(std::vector<T>(width * height * channels), width, height));
        }

        static std::shared_ptr<DataTexture> create(
                ImageData data,
                unsigned int width, unsigned int height) {

            return std::shared_ptr<DataTexture>(new DataTexture(std::move(data), width, height));
        }

    private:
        explicit DataTexture(ImageData data, unsigned int width, unsigned int height)
            : Texture(makeImages(std::move(data), width, height)) {
            this->magFilter = Filter::Nearest;
            this->minFilter = Filter::Nearest;

            this->generateMipmaps = false;
            this->unpackAlignment = 1;

            // this->colorSpace = ColorSpace::sRGB;

            this->needsUpdate();
        }

        static std::vector<Image> makeImages(ImageData data, unsigned int width, unsigned int height) {

            std::vector<Image> images;
            images.emplace_back(std::move(data), width, height);
            return images;
        }
    };

}// namespace threepp

#endif//THREEPP_DATATEXTURE_HPP
