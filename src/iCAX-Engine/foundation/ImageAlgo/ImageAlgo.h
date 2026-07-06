#pragma once

#include "framework.h"
#include "../ImageAdapter/ImageAdapter.h"
#include "../ImageData/ImageData.h"

#include <cstdint>
#include <string>

namespace iCAX::ImageAlgo
{
    enum class EBlendMode : std::uint8_t
    {
        Normal,
        Multiply,
        Screen,
        Overlay,
        Darken,
        Lighten
    };

    IMAGEALGO_API std::uint32_t GetChannelCount(ImageData::EImagePixelFormat pixelFormat);
    IMAGEALGO_API std::uint32_t GetBitsPerChannel(ImageData::EImagePixelFormat pixelFormat);
    IMAGEALGO_API std::uint32_t GetBytesPerPixel(ImageData::EImagePixelFormat pixelFormat);

    IMAGEALGO_API bool Validate(const ImageData::BitmapImage& image, std::string& error);
    IMAGEALGO_API bool Validate(const ImageData::RGBAImage& image, std::string& error);
    IMAGEALGO_API bool Validate(const ImageData::VectorImage& image, std::string& error);

    IMAGEALGO_API ImageData::RGBAImage MakeRGBAImage(
        std::uint32_t width,
        std::uint32_t height,
        ImageData::EImageColorSpace colorSpace = ImageData::EImageColorSpace::SRGB);

    IMAGEALGO_API bool TryBlendRGBA(
        const ImageData::RGBAImage& background,
        const ImageData::RGBAImage& foreground,
        ImageData::RGBAImage& output,
        std::string& error,
        EBlendMode blendMode = EBlendMode::Normal,
        double opacity = 1.0);

    IMAGEALGO_API bool TryRasterize(
        const ImageData::VectorImage& image,
        ImageData::ImageExtent2D targetExtent,
        ImageData::RGBAImage& output,
        std::string& error,
        const ImageAdapter::IImageKernelAdapter* adapter);
}
