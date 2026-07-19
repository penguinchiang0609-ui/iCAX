#include "pch.h"
#include "ImageAlgo.h"


namespace
{
    std::uint8_t ClampByte(const double value)
    {
        return static_cast<std::uint8_t>(std::clamp(std::round(value), 0.0, 255.0));
    }

    double ToUnit(const std::uint8_t value)
    {
        return static_cast<double>(value) / 255.0;
    }

    double BlendChannel(const double background, const double foreground, const iCAX::ImageAlgo::EBlendMode blendMode)
    {
        switch (blendMode)
        {
        case iCAX::ImageAlgo::EBlendMode::Multiply:
            return background * foreground;
        case iCAX::ImageAlgo::EBlendMode::Screen:
            return 1.0 - (1.0 - background) * (1.0 - foreground);
        case iCAX::ImageAlgo::EBlendMode::Overlay:
            return background <= 0.5
                ? 2.0 * background * foreground
                : 1.0 - 2.0 * (1.0 - background) * (1.0 - foreground);
        case iCAX::ImageAlgo::EBlendMode::Darken:
            return std::min(background, foreground);
        case iCAX::ImageAlgo::EBlendMode::Lighten:
            return std::max(background, foreground);
        case iCAX::ImageAlgo::EBlendMode::Normal:
        default:
            return foreground;
        }
    }
}

namespace iCAX::ImageAlgo
{
    std::uint32_t GetChannelCount(const ImageData::EImagePixelFormat pixelFormat)
    {
        switch (pixelFormat)
        {
        case ImageData::EImagePixelFormat::Gray8:
            return 1;
        case ImageData::EImagePixelFormat::GrayAlpha8:
            return 2;
        case ImageData::EImagePixelFormat::RGB8:
        case ImageData::EImagePixelFormat::BGR8:
            return 3;
        case ImageData::EImagePixelFormat::RGBA8:
        case ImageData::EImagePixelFormat::BGRA8:
        case ImageData::EImagePixelFormat::RGBA16:
        case ImageData::EImagePixelFormat::RGBA32Float:
            return 4;
        case ImageData::EImagePixelFormat::Unknown:
        default:
            return 0;
        }
    }

    std::uint32_t GetBitsPerChannel(const ImageData::EImagePixelFormat pixelFormat)
    {
        switch (pixelFormat)
        {
        case ImageData::EImagePixelFormat::Gray8:
        case ImageData::EImagePixelFormat::GrayAlpha8:
        case ImageData::EImagePixelFormat::RGB8:
        case ImageData::EImagePixelFormat::BGR8:
        case ImageData::EImagePixelFormat::RGBA8:
        case ImageData::EImagePixelFormat::BGRA8:
            return 8;
        case ImageData::EImagePixelFormat::RGBA16:
            return 16;
        case ImageData::EImagePixelFormat::RGBA32Float:
            return 32;
        case ImageData::EImagePixelFormat::Unknown:
        default:
            return 0;
        }
    }

    std::uint32_t GetBytesPerPixel(const ImageData::EImagePixelFormat pixelFormat)
    {
        const auto bits = GetBitsPerChannel(pixelFormat) * GetChannelCount(pixelFormat);
        return bits == 0 ? 0 : bits / 8;
    }

    bool Validate(const ImageData::BitmapImage& image, std::string& error)
    {
        if (image.Extent.Width == 0 || image.Extent.Height == 0)
        {
            error = "Bitmap image extent must be non-zero.";
            return false;
        }

        const auto bytesPerPixel = GetBytesPerPixel(image.Layout.PixelFormat);
        if (bytesPerPixel == 0)
        {
            error = "Bitmap image pixel format is invalid.";
            return false;
        }

        if (image.Planes.empty())
        {
            error = "Bitmap image must contain at least one plane.";
            return false;
        }

        const auto minimumStride = static_cast<std::uint64_t>(image.Extent.Width) * bytesPerPixel;
        if (image.Planes.front().StrideBytes < minimumStride)
        {
            error = "Bitmap image stride is smaller than one row.";
            return false;
        }

        if (image.Pixels.size() < image.Planes.front().OffsetBytes + image.Planes.front().SizeBytes)
        {
            error = "Bitmap image pixel buffer is smaller than plane size.";
            return false;
        }

        error.clear();
        return true;
    }

    bool Validate(const ImageData::RGBAImage& image, std::string& error)
    {
        if (image.Extent.Width == 0 || image.Extent.Height == 0)
        {
            error = "RGBA image extent must be non-zero.";
            return false;
        }

        const auto minimumStride = static_cast<std::uint64_t>(image.Extent.Width) * 4u;
        if (image.StrideBytes < minimumStride)
        {
            error = "RGBA image stride is smaller than one row.";
            return false;
        }

        const auto requiredSize = image.StrideBytes * image.Extent.Height;
        if (image.Pixels.size() < requiredSize)
        {
            error = "RGBA image pixel buffer is smaller than extent.";
            return false;
        }

        error.clear();
        return true;
    }

    bool Validate(const ImageData::VectorImage& image, std::string& error)
    {
        if (image.Viewport.Width <= 0.0 || image.Viewport.Height <= 0.0)
        {
            error = "Vector image viewport must be non-zero.";
            return false;
        }

        error.clear();
        return true;
    }

    ImageData::RGBAImage MakeRGBAImage(
        const std::uint32_t width,
        const std::uint32_t height,
        const ImageData::EImageColorSpace colorSpace)
    {
        ImageData::RGBAImage image;
        image.Extent = { width, height };
        image.ColorSpace = colorSpace;
        image.AlphaMode = ImageData::EImageAlphaMode::Straight;
        image.Origin = ImageData::EImageOrigin::TopLeft;
        image.StrideBytes = static_cast<std::uint64_t>(width) * 4u;
        image.Pixels.resize(static_cast<std::size_t>(image.StrideBytes * height), 0);
        return image;
    }

    bool TryBlendRGBA(
        const ImageData::RGBAImage& background,
        const ImageData::RGBAImage& foreground,
        ImageData::RGBAImage& output,
        std::string& error,
        const EBlendMode blendMode,
        const double opacity)
    {
        if (!Validate(background, error))
        {
            return false;
        }

        if (!Validate(foreground, error))
        {
            return false;
        }

        if (background.Extent.Width != foreground.Extent.Width || background.Extent.Height != foreground.Extent.Height)
        {
            error = "RGBA images must have the same extent.";
            return false;
        }

        const auto actualOpacity = std::clamp(opacity, 0.0, 1.0);
        output = MakeRGBAImage(background.Extent.Width, background.Extent.Height, background.ColorSpace);

        for (std::uint32_t y = 0; y < background.Extent.Height; ++y)
        {
            const auto backgroundRow = y * background.StrideBytes;
            const auto foregroundRow = y * foreground.StrideBytes;
            const auto outputRow = y * output.StrideBytes;
            for (std::uint32_t x = 0; x < background.Extent.Width; ++x)
            {
                const auto bi = static_cast<std::size_t>(backgroundRow + x * 4u);
                const auto fi = static_cast<std::size_t>(foregroundRow + x * 4u);
                const auto oi = static_cast<std::size_t>(outputRow + x * 4u);
                const auto foregroundAlpha = ToUnit(foreground.Pixels[fi + 3]) * actualOpacity;
                const auto backgroundAlpha = ToUnit(background.Pixels[bi + 3]);
                const auto outAlpha = foregroundAlpha + backgroundAlpha * (1.0 - foregroundAlpha);

                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    const auto blended = BlendChannel(ToUnit(background.Pixels[bi + channel]), ToUnit(foreground.Pixels[fi + channel]), blendMode);
                    const auto out = outAlpha <= 0.0
                        ? 0.0
                        : (blended * foregroundAlpha + ToUnit(background.Pixels[bi + channel]) * backgroundAlpha * (1.0 - foregroundAlpha)) / outAlpha;
                    output.Pixels[oi + channel] = ClampByte(out * 255.0);
                }

                output.Pixels[oi + 3] = ClampByte(outAlpha * 255.0);
            }
        }

        error.clear();
        return true;
    }

    bool TryRasterize(
        const ImageData::VectorImage& image,
        const ImageData::ImageExtent2D targetExtent,
        ImageData::RGBAImage& output,
        std::string& error,
        const ImageAdapter::IImageKernelAdapter* adapter)
    {
        if (!adapter)
        {
            error = "Image adapter is required to rasterize vector image.";
            return false;
        }

        if (!Validate(image, error))
        {
            return false;
        }

        return adapter->TryRasterize(image, targetExtent, output, error);
    }
}
