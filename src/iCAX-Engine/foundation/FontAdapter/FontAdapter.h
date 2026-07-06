#pragma once

#include "framework.h"
#include "../FontData/FontData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace iCAX::FontAdapter
{
    class FONTADAPTER_API IFontKernelAdapter
    {
    public:
        virtual ~IFontKernelAdapter() = default;

        virtual bool TryLoadFace(
            const std::vector<std::uint8_t>& fontData,
            std::uint32_t faceIndex,
            FontData::FontFace& face,
            std::string& error) const = 0;

        virtual bool TryGetGlyphOutline(
            const std::vector<std::uint8_t>& fontData,
            std::uint32_t faceIndex,
            FontData::GlyphID glyph,
            FontData::GlyphOutline& outline,
            std::string& error) const = 0;

        virtual bool TryGetGlyphBitmap(
            const std::vector<std::uint8_t>& fontData,
            std::uint32_t faceIndex,
            FontData::GlyphID glyph,
            double pixelSize,
            FontData::GlyphBitmap& bitmap,
            std::string& error) const = 0;

        virtual bool TryShapeText(
            const FontData::FontFace& face,
            const FontData::TextRun& run,
            FontData::ShapedText& shapedText,
            std::string& error) const = 0;
    };
}
