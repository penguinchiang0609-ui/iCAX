#pragma once

#include "../GeometryData/GeometryData.h"
#include "../ImageData/ImageData.h"

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace iCAX::FontData
{
    enum class EFontFaceKind : std::uint8_t
    {
        Unknown,
        Outline,
        Bitmap,
        Mixed
    };

    enum class EFontSlant : std::uint8_t
    {
        Upright,
        Italic,
        Oblique
    };

    enum class EFontGlyphKind : std::uint8_t
    {
        Unknown,
        Outline,
        Bitmap
    };

    enum class ETextDirection : std::uint8_t
    {
        LeftToRight,
        RightToLeft,
        TopToBottom,
        BottomToTop
    };

    using GlyphID = std::uint32_t;
    using CodePoint = std::uint32_t;

    struct FontFaceInfo
    {
        std::string FamilyName;
        std::string SubfamilyName;
        std::string FullName;
        std::string PostScriptName;
        EFontFaceKind FaceKind = EFontFaceKind::Unknown;
        EFontSlant Slant = EFontSlant::Upright;
        std::uint16_t Weight = 400;
        std::uint16_t Stretch = 5;
        std::uint16_t UnitsPerEm = 1000;
        std::int32_t Ascender = 0;
        std::int32_t Descender = 0;
        std::int32_t LineGap = 0;
        std::uint32_t GlyphCount = 0;
    };

    struct GlyphMetrics
    {
        double AdvanceX = 0.0;
        double AdvanceY = 0.0;
        double BearingX = 0.0;
        double BearingY = 0.0;
        double Width = 0.0;
        double Height = 0.0;
    };

    struct GlyphOutline
    {
        std::vector<GeometryData::Loop2> Contours;
        ImageData::EImageFillRule FillRule = ImageData::EImageFillRule::NonZero;
    };

    struct GlyphBitmap
    {
        ImageData::BitmapImage Bitmap;
        double PixelSize = 0.0;
    };

    using GlyphShape = std::variant<GlyphOutline, GlyphBitmap>;

    struct Glyph
    {
        GlyphID ID = 0;
        EFontGlyphKind Kind = EFontGlyphKind::Unknown;
        GlyphMetrics Metrics;
        GlyphShape Shape;
        std::vector<CodePoint> CodePoints;
        std::map<std::string, std::string> Metadata;
    };

    struct FontFace
    {
        inline static constexpr const char* kResourceTypeName = "font.face";

        FontFaceInfo Info;
        std::vector<Glyph> Glyphs;
        std::map<std::string, std::string> Metadata;
        std::uint64_t DataVersion = 0;
    };

    struct TextStyle
    {
        std::string FamilyName;
        double FontSize = 12.0;
        std::uint16_t Weight = 400;
        EFontSlant Slant = EFontSlant::Upright;
        double LetterSpacing = 0.0;
        double WordSpacing = 0.0;
    };

    struct TextRun
    {
        std::u32string Text;
        TextStyle Style;
        std::string Language;
        std::string Script;
        ETextDirection Direction = ETextDirection::LeftToRight;
    };

    struct GlyphPlacement
    {
        GlyphID Glyph = 0;
        std::uint32_t Cluster = 0;
        double OffsetX = 0.0;
        double OffsetY = 0.0;
        double AdvanceX = 0.0;
        double AdvanceY = 0.0;
    };

    struct ShapedText
    {
        TextRun SourceRun;
        std::vector<GlyphPlacement> Glyphs;
        double AdvanceX = 0.0;
        double AdvanceY = 0.0;
        std::map<std::string, std::string> Metadata;
    };
}
