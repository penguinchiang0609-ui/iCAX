#pragma once

#include "../GeometryData/GeometryData.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace iCAX::ImageData
{
    enum class EImageKind : std::uint8_t
    {
        Unknown,
        Bitmap,
        Vector
    };

    enum class EImagePixelFormat : std::uint8_t
    {
        Unknown,
        Gray8,
        GrayAlpha8,
        RGB8,
        BGR8,
        RGBA8,
        BGRA8,
        RGBA16,
        RGBA32Float
    };

    enum class EImageColorSpace : std::uint8_t
    {
        Unknown,
        Linear,
        SRGB
    };

    enum class EImageAlphaMode : std::uint8_t
    {
        Unknown,
        None,
        Straight,
        Premultiplied
    };

    enum class EImageFillRule : std::uint8_t
    {
        NonZero,
        EvenOdd
    };

    enum class EImageOrigin : std::uint8_t
    {
        TopLeft,
        BottomLeft
    };

    enum class EVectorLineCap : std::uint8_t
    {
        Butt,
        Round,
        Square
    };

    enum class EVectorLineJoin : std::uint8_t
    {
        Miter,
        Round,
        Bevel
    };

    enum class EVectorPaintKind : std::uint8_t
    {
        None,
        SolidColor
    };

    struct ImageExtent2D
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
    };

    struct VectorViewport
    {
        double X = 0.0;
        double Y = 0.0;
        double Width = 0.0;
        double Height = 0.0;
    };

    struct ImageLayout
    {
        EImagePixelFormat PixelFormat = EImagePixelFormat::Unknown;
        EImageColorSpace ColorSpace = EImageColorSpace::Unknown;
        EImageAlphaMode AlphaMode = EImageAlphaMode::Unknown;
        EImageOrigin Origin = EImageOrigin::TopLeft;
        std::uint32_t ChannelCount = 0;
        std::uint32_t BitsPerChannel = 0;
        std::uint32_t BytesPerPixel = 0;
    };

    struct ImagePlane
    {
        std::uint64_t OffsetBytes = 0;
        std::uint64_t StrideBytes = 0;
        std::uint64_t SizeBytes = 0;
    };

    struct BitmapImage
    {
        inline static constexpr const char* kResourceTypeName = "image.bitmap";

        ImageExtent2D Extent;
        ImageLayout Layout;
        std::vector<ImagePlane> Planes;
        std::vector<std::uint8_t> Pixels;
        std::map<std::string, std::string> Metadata;
        std::uint64_t DataVersion = 0;
    };

    struct RGBAImage
    {
        inline static constexpr const char* kResourceTypeName = "image.rgba";

        ImageExtent2D Extent;
        EImageColorSpace ColorSpace = EImageColorSpace::SRGB;
        EImageAlphaMode AlphaMode = EImageAlphaMode::Straight;
        EImageOrigin Origin = EImageOrigin::TopLeft;
        std::uint64_t StrideBytes = 0;
        std::vector<std::uint8_t> Pixels;
        std::map<std::string, std::string> Metadata;
        std::uint64_t DataVersion = 0;
    };

    struct VectorColor
    {
        double R = 0.0;
        double G = 0.0;
        double B = 0.0;
        double A = 1.0;
    };

    struct VectorPaint
    {
        EVectorPaintKind Kind = EVectorPaintKind::None;
        VectorColor Color;
    };

    struct VectorFill
    {
        VectorPaint Paint;
        EImageFillRule FillRule = EImageFillRule::NonZero;
    };

    struct VectorStroke
    {
        VectorPaint Paint;
        double Width = 1.0;
        EVectorLineCap LineCap = EVectorLineCap::Butt;
        EVectorLineJoin LineJoin = EVectorLineJoin::Miter;
        double MiterLimit = 4.0;
        std::vector<double> DashArray;
        double DashOffset = 0.0;
    };

    struct VectorPath
    {
        std::vector<GeometryData::CompositeCurve2> Contours;
        EImageFillRule FillRule = EImageFillRule::NonZero;
    };

    struct VectorElement
    {
        std::uint64_t ElementID = 0;
        std::string Name;
        GeometryData::Transform2 Transform;
        VectorPath Path;
        std::optional<VectorFill> Fill;
        std::optional<VectorStroke> Stroke;
        std::map<std::string, std::string> Metadata;
    };

    struct VectorImage
    {
        inline static constexpr const char* kResourceTypeName = "image.vector";

        VectorViewport Viewport;
        std::vector<VectorElement> Elements;
        std::map<std::string, std::string> Metadata;
        std::uint64_t DataVersion = 0;
    };

    using ImageContent = std::variant<BitmapImage, RGBAImage, VectorImage>;

    struct ImageResource
    {
        inline static constexpr const char* kResourceTypeName = "image";

        EImageKind Kind = EImageKind::Unknown;
        ImageContent Content;
        std::map<std::string, std::string> Metadata;
        std::uint64_t DataVersion = 0;
    };
}
