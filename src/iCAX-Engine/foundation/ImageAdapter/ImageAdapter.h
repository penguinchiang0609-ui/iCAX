#pragma once

#include "framework.h"
#include "../ImageData/ImageData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX::ImageAdapter
{
    class IMAGEADAPTER_API IImageKernelAdapter
    {
    public:
        virtual ~IImageKernelAdapter() = default;

        virtual bool TryDecodeBitmap(
            const std::vector<std::uint8_t>& encodedData,
            ImageData::BitmapImage& image,
            std::string& error) const = 0;

        virtual bool TryDecodeVector(
            const std::vector<std::uint8_t>& encodedData,
            ImageData::VectorImage& image,
            std::string& error) const = 0;

        virtual bool TryEncodeBitmap(
            const ImageData::BitmapImage& image,
            const std::string& formatId,
            std::vector<std::uint8_t>& encodedData,
            std::string& error) const = 0;

        virtual bool TryRasterize(
            const ImageData::VectorImage& image,
            ImageData::ImageExtent2D targetExtent,
            ImageData::RGBAImage& output,
            std::string& error) const = 0;
    };
}
